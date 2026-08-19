/**
 * @file    WebUI.h
 * @brief   Build-time source for the embedded web UI — DO NOT include directly.
 * @details Fontes HTML/CSS/JS como literais PROGMEM. Consumido APENAS por
 *          `tools/build_webui_gz.py` (pre:script PlatformIO) que gera
 *          `WebUI_GZ.h` com versões gzipadas. O firmware liga apenas contra
 *          `WebUI_GZ.h` (gzip enviado direto ao cliente via Content-Encoding).
 *
 *          Auditoria MEM-003 / F17 etapa 6 (2026-04-25): zero `#include
 *          "WebUI.h"` no projeto; zero referências a `WebUI::` no código;
 *          0 símbolos `WebUI::` no firmware.elf. O linker já omite as ~291 KB
 *          de raw porque ninguém referencia. O `#error` abaixo é guarda de
 *          regressão: se algum .cpp futuro acidentalmente incluir este header,
 *          a build falha imediatamente em vez de inflar flash silenciosamente.
 *
 *          Para regenerar `WebUI_GZ.h` após editar este arquivo, basta rodar
 *          `pio run` (o pre:script detecta mtime). Para regenerar manualmente:
 *          `python3 tools/build_webui_gz.py`.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

/* MEM-003: guarda contra inclusão acidental — WebUI.h é build-time only.
 * Use `#include "WebUI_GZ.h"` e os símbolos `WebUI_GZ::*_GZ` em vez disso.
 * Bypassa só o pre-script Python (que lê texto, não pré-processa C++). */
#ifndef SIMUT_WEBUI_BUILD_TIME_ONLY
#error "WebUI.h é build-time only — use WebUI_GZ.h em vez disso. (MEM-003)"
#endif


namespace WebUI {


static const char LOGIN_PAGE[] PROGMEM = R"raw(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate">
    <title>SIMUT - Login</title>
    <script>if(localStorage.getItem('simut_ui_theme')==='light')document.documentElement.classList.add('light')</script>
    <style>
        :root { --bg: #0c0f13; --card: #161b22; --txt: #e9edf2; --sub: #98a6b3; --acc: #06b6d4; --dang: #ef4444; --border: #2a3340; color-scheme: dark; }
        /* Tema claro: pre-sessao nao carrega o lang.js, entao a classe vem do
           script acima (mesma chave simut_ui_theme) e as regras moram aqui. */
        html.light { --bg: #f2f5f8; --card: #ffffff; --txt: #1b2733; --sub: #526172; --acc: #0072cd; --dang: #c93838; --border: #d9e1e8; color-scheme: light; }
        html.light input[type="text"], html.light input[type="password"] { background: #fff; border-color: var(--border); color: var(--txt); }
        html.light .box { box-shadow: 0 10px 30px rgba(27,39,51,0.12); }
        html.light button[type="submit"] { color: #fff; }
        html.light button[type="submit"]:disabled { background: #cbd5e1; color: #94a3b8; }
        html.light .lang-box select { background: #fff; color: var(--sub); }
        html.light .bar-bg { background: #e3eaf0; }
        html.light .req-list span.ok, html.light .ok-msg { color: #15803d; }
        /* margin:auto no lugar de align-items:center — com conteudo mais alto que a
           viewport, centralizar por flex joga o topo fora do alcance da rolagem. */
        body { background: var(--bg); color: var(--txt); font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; display: flex; min-height: 100vh; min-height: 100dvh; margin: 0; padding: 16px; box-sizing: border-box; }
        .box { background: var(--card); margin: auto; padding: clamp(20px, 5vw, 40px); border-radius: 12px; border: 1px solid var(--border); width: min(340px, 100%); box-sizing: border-box; text-align: center; box-shadow: 0 10px 30px rgba(0,0,0,0.5); }
        /* .box ja e text-align:center, entao marca e legenda centralizam sozinhas.
           line-height:1 evita a folga que 3.6rem abriria acima do texto. */
        /* A marca e vetor, nao texto: a pilha de fontes do sistema entrega uma
           fonte diferente em cada SO (SF, Segoe, Roboto...) e a marca mudava de
           desenho conforme o navegador. Traçado do Liberation Sans Bold, que e
           metricamente Arial — o mais proximo do que a maioria ja renderizava.
           210px reproduz a largura que o texto tinha a 4.4rem. fill=currentColor,
           entao a cor vem daqui e acompanha --acc. */
        .brand { width: min(210px, 100%); margin: 0 auto 6px; color: var(--acc); }
        .brand svg { width: 100%; height: auto; display: block; }
        /* text-wrap:balance — a legenda cabe numa linha por ~2px a 360px; numa
           metrica de fonte um pouco diferente ela quebra, e sem isto sobraria
           uma palavra orfa na 2a linha. Navegador antigo ignora e quebra normal. */
        .tagline { font-size: 0.78rem; color: var(--sub); line-height: 1.45; margin-bottom: 28px; text-wrap: balance; }
        input[type="text"], input[type="password"] { width: 100%; padding: 14px; margin: 10px 0; background: #000; border: 1px solid #3f3f46; color: white; border-radius: 8px; box-sizing: border-box; font-size: 1rem; transition: 0.2s; }
        input:focus { border-color: var(--acc); outline: none; }
        button[type="submit"] { width: 100%; padding: 14px; background: var(--acc); color: #000; font-weight: bold; border: none; border-radius: 8px; cursor: pointer; margin-top: 15px; font-size: 1.05rem; transition: 0.2s; }
        button[type="submit"]:hover { opacity: 0.9; transform: translateY(-1px); }
        button[type="submit"]:disabled { background: #3f3f46; color: #a1a1aa; cursor: not-allowed; }
        .err { color: var(--dang); font-size: 0.9rem; margin-top: 12px; min-height: 1.2em; }
        .chk-row { display: flex; align-items: center; gap: 8px; margin-bottom: 10px; justify-content: flex-start; }
        .chk-row input { width: 16px; height: 16px; accent-color: var(--acc); cursor: pointer; margin: 0;}
        .chk-row label { color: var(--sub); font-size: 0.85rem; cursor: pointer; }
        .lang-box { display: flex; align-items: center; justify-content: center; gap: 8px; margin-top: 25px; padding-top: 20px; border-top: 1px solid var(--border); }
        /* 16px: abaixo disso o iOS da zoom no foco e nao volta. */
        .lang-box select { background: #000; color: var(--sub); border: 1px solid var(--border); padding: 6px 10px; border-radius: 6px; outline: none; cursor: pointer; font-size: 16px;}
        .lang-box select:focus { border-color: var(--acc); color: var(--txt); }
        .toggle-link { color: var(--acc); text-decoration: none; font-size: 0.85rem; margin-top: 14px; display: inline-block; cursor: pointer; }
        .toggle-link:hover { text-decoration: underline; }
        .bar-bg { width: 100%; height: 6px; background: #3f3f46; border-radius: 4px; margin-top: 6px; overflow: hidden; }
        .bar-fg { height: 100%; width: 0%; transition: 0.3s; background: #ef4444; }
        .req-list { text-align: left; font-size: 0.78rem; color: var(--sub); margin-top: 6px; line-height: 1.5; }
        .req-list span { display: block; }
        .req-list span.ok { color: #22c55e; }
        .req-list span.ok::before { content: "\2713 "; }
        .req-list span:not(.ok)::before { content: "\2715 "; color: var(--dang); }
        .ok-msg { color: #22c55e; font-size: 0.9rem; margin-top: 12px; min-height: 1.2em; }
    </style>
    <script>
    /* F-LANGPACK β: dict.pt vem de GET /api/lang (servido do .lng). */
    const dictLog = {
        pt: {},
        en: { "log_usr": "Username", "log_pas": "Password", "log_show": "Show password", "log_btn": "Sign In", "log_err": "Invalid credentials.", "log_full": "System is full. Try again later.", "log_lock": "Locked for {s}s. Too many attempts.", "log_chpass_link": "Change password", "log_chpass_title": "Change Password", "log_oldpass": "Current Password", "log_newpass": "New Password", "log_newpass2": "Repeat New Password", "log_chpass_btn": "Save New Password", "log_chpass_back": "← Back to Login", "log_chpass_ok": "Password changed. Please sign in.", "log_chpass_same": "New password must differ from current.", "log_chpass_mismatch": "Passwords do not match.", "log_chpass_req_len": "At least 8 characters", "log_chpass_req_letter": "Letter", "log_chpass_req_digit": "Digit", "log_chpass_req_symbol": "Symbol" }
    };
    fetch('/api/lang').then(r=>r.json()).then(d=>{Object.assign(dictLog.pt,d);applyLang();}).catch(()=>{});
    function t(key, fallback, vars) { let l = localStorage.getItem('simut_lang') || 'en'; let s = (l !== 'en' && dictLog[l] && dictLog[l][key]) ? dictLog[l][key] : fallback; if (vars) for (let k in vars) s = s.replace('{'+k+'}', vars[k]); return s; }
    function setLang(l) { localStorage.setItem('simut_lang', l); applyLang(); }
    function applyLang() { let l = localStorage.getItem('simut_lang') || 'en'; document.querySelectorAll('.lang-select').forEach(s => s.value = l); document.querySelectorAll('[data-i18n]').forEach(el => { let k = el.getAttribute('data-i18n'); if(!el.hasAttribute('data-en')) el.setAttribute('data-en', el.placeholder || el.innerHTML); let txt = (l === 'en' || !dictLog[l] || !dictLog[l][k]) ? el.getAttribute('data-en') : dictLog[l][k]; if(el.tagName === 'INPUT') el.placeholder = txt; else el.innerHTML = txt; }); }
    function sha256(ascii){function rightRotate(value,amount){return(value>>>amount)|(value<<(32-amount));}var mathPow=Math.pow;var maxWord=mathPow(2,32);var lengthProperty='length';var i,j;var result='';var words=[];var asciiBitLength=ascii[lengthProperty]*8;var hash=sha256.h=sha256.h||[];var k=sha256.k=sha256.k||[];var primeCounter=k[lengthProperty];var isComposite={};for(var candidate=2;primeCounter<64;candidate++){if(!isComposite[candidate]){for(i=0;i<313;i+=candidate)isComposite[i]=candidate;hash[primeCounter]=(mathPow(candidate,.5)*maxWord)|0;k[primeCounter++]=(mathPow(candidate,1/3)*maxWord)|0;}}ascii+='\x80';while(ascii[lengthProperty]%64-56)ascii+='\x00';for(i=0;i<ascii[lengthProperty];i++){j=ascii.charCodeAt(i);if(j>>8)return;words[i>>2]|=j<<((3-i)%4)*8;}words[words[lengthProperty]]=((asciiBitLength/maxWord)|0);words[words[lengthProperty]]=(asciiBitLength);for(j=0;j<words[lengthProperty];){var w=words.slice(j,j+=16);var oldHash=hash;hash=hash.slice(0,8);for(i=0;i<64;i++){var w15=w[i-15],w2=w[i-2];var a=hash[0],e=hash[4];var temp1=hash[7]+(rightRotate(e,6)^rightRotate(e,11)^rightRotate(e,25))+((e&hash[5])^((~e)&hash[6]))+k[i]+(w[i]=(i<16)?w[i]:(w[i-16]+(rightRotate(w15,7)^rightRotate(w15,18)^(w15>>>3))+w[i-7]+(rightRotate(w2,17)^rightRotate(w2,19)^(w2>>>10)))|0);var temp2=(rightRotate(a,2)^rightRotate(a,13)^rightRotate(a,22))+((a&hash[1])^(a&hash[2])^(hash[1]&hash[2]));hash=[(temp1+temp2)|0].concat(hash);hash[4]=(hash[4]+temp1)|0;}for(i=0;i<8;i++)hash[i]=(hash[i]+oldHash[i])|0;}for(i=0;i<8;i++){for(j=3;j+1;j--){var b=(hash[i]>>(j*8))&255;result+=((b<16)?0:'')+b.toString(16);}}return result;}

    let _nonce = '', _lockTimer = null;
    async function fetchNonce() { try { let r = await fetch('/api/login_init', { credentials: 'same-origin' }); let j = await r.json(); _nonce = j.nonce || ''; if (j.locked && j.lockSec > 0) showLockout(j.lockSec); } catch(e) { _nonce = ''; } }
    function showError(msg) { document.getElementById('errMsg').textContent = msg; }
    function showLockout(sec) { let btn = document.getElementById('btnLogin'); btn.disabled = true; if (_lockTimer) clearInterval(_lockTimer); let rem = sec; showError(t('log_lock', 'Locked for {s}s.', {s: rem})); _lockTimer = setInterval(() => { rem--; if (rem <= 0) { clearInterval(_lockTimer); _lockTimer = null; btn.disabled = false; showError(''); fetchNonce(); } else { showError(t('log_lock', 'Locked for {s}s.', {s: rem})); } }, 1000); }

    async function doLogin(e) {
        e.preventDefault(); let btn = document.getElementById('btnLogin'); if (btn.disabled) return;
        let user = document.querySelector('input[name="user"]').value.trim(); let passEl = document.getElementById('passInput'); let pass = passEl.value;
        if (!user || !pass) return; if (pass.length !== 64) pass = sha256(pass);
        showError(''); btn.disabled = true;
        try {
            let fd = new URLSearchParams(); fd.append('user', user); fd.append('pass', pass); fd.append('nonce', _nonce);
            let r = await fetch('/api/login', { method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body: fd.toString(), credentials: 'same-origin' });
            let j = await r.json();
            if (j.ok) {
                /* Uncommitted edits live in sessionStorage so they can survive
                   moving between the config pages before one Save and Restart.
                   sessionStorage outlives a logout and a reload, though, so a
                   fresh sign-in used to inherit whatever the last one had typed
                   and never applied. Signing in means: show me the device as it
                   is now. */
                try {
                    sessionStorage.removeItem('simut_pending');
                    sessionStorage.removeItem('simut_pending_notified');
                } catch(e) {}
                window.location.href = j.redirect || '/';
                return;
            }
            await fetchNonce();
            if (j.err === 2 && j.lockSec > 0) showLockout(j.lockSec); else if (j.err === 3) { showError(t('log_full', 'System full.')); btn.disabled = false; } else { showError(t('log_err', 'Invalid credentials.')); btn.disabled = false; }
        } catch(ex) { showError('Connection error.'); btn.disabled = false; await fetchNonce(); }
        passEl.value = '';
    }
    function togglePass() { document.getElementById('passInput').type = document.getElementById('chkPass').checked ? 'text' : 'password'; }

    function setMode(m) {
        document.getElementById('loginForm').style.display = (m === 'login') ? '' : 'none';
        document.getElementById('chpassForm').style.display = (m === 'chpass') ? '' : 'none';
        document.getElementById('lnkChpass').style.display = (m === 'login') ? '' : 'none';
        showError(''); document.getElementById('chErr').textContent = ''; document.getElementById('chOk').textContent = '';
    }
    function togglePass2() {
        let t = document.getElementById('chkPass2').checked ? 'text' : 'password';
        document.getElementById('opInput').type = t; document.getElementById('np1').type = t; document.getElementById('np2').type = t;
    }
    function chpassStrength() {
        let p = document.getElementById('np1').value;
        let p2 = document.getElementById('np2').value;
        let len = p.length >= 8, letter = /[A-Za-z]/.test(p), digit = /[0-9]/.test(p), symbol = /[^A-Za-z0-9]/.test(p);
        let score = (len?1:0) + (letter?1:0) + (digit?1:0) + (symbol?1:0);
        let bar = document.getElementById('chBar'); bar.style.width = (score*25) + '%';
        bar.style.background = score <= 1 ? '#ef4444' : score === 2 ? '#f59e0b' : score === 3 ? '#3b82f6' : '#22c55e';
        document.getElementById('rqLen').classList.toggle('ok', len);
        document.getElementById('rqLet').classList.toggle('ok', letter);
        document.getElementById('rqDig').classList.toggle('ok', digit);
        document.getElementById('rqSym').classList.toggle('ok', symbol);
        let strong = (score === 4);
        let match = (p.length > 0 && p === p2);
        document.getElementById('btnChpass').disabled = !(strong && match);
        let err = document.getElementById('chErr');
        if (p2.length > 0 && p !== p2) err.textContent = t('log_chpass_mismatch','Passwords do not match.'); else err.textContent = '';
    }
    async function doChpass(e) {
        e.preventDefault();
        let btn = document.getElementById('btnChpass'); if (btn.disabled) return;
        let user = document.querySelector('input[name="user2"]').value.trim();
        let opEl = document.getElementById('opInput'), np1El = document.getElementById('np1'), np2El = document.getElementById('np2');
        let op = opEl.value, np = np1El.value;
        if (!user || !op || !np) return;
        /* Over HTTPS send the plaintext on the encrypted channel so the server
           can enforce the password policy (A-5); over HTTP send the sha256 as
           before, so nothing readable crosses a cleartext link. */
        let secure = location.protocol === 'https:';
        let opSend = secure ? op : sha256(op);
        let npSend = secure ? np : sha256(np);
        if (opSend === npSend) { document.getElementById('chErr').textContent = t('log_chpass_same','New password must differ from current.'); return; }
        btn.disabled = true; document.getElementById('chErr').textContent = '';
        try {
            let fd = new URLSearchParams();
            fd.append('user', user); fd.append('oldpass', opSend); fd.append('newpass', npSend); fd.append('nonce', _nonce);
            let r = await fetch('/api/login_chpass', { method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body: fd.toString(), credentials: 'same-origin' });
            let j = await r.json();
            await fetchNonce();
            if (j.ok) {
                opEl.value = ''; np1El.value = ''; np2El.value = '';
                chpassStrength();
                document.getElementById('chOk').textContent = t('log_chpass_ok','Password changed. Please sign in.');
                setTimeout(() => { setMode('login'); document.querySelector('input[name="user"]').value = user; document.getElementById('passInput').focus(); }, 1500);
            } else {
                if (j.err === 2 && j.lockSec > 0) { setMode('login'); showLockout(j.lockSec); }
                else if (j.err === 5) document.getElementById('chErr').textContent = t('log_chpass_same','New password must differ from current.');
                else document.getElementById('chErr').textContent = t('log_err','Invalid credentials.');
                btn.disabled = false;
            }
        } catch(ex) { document.getElementById('chErr').textContent = 'Connection error.'; btn.disabled = false; await fetchNonce(); }
    }
    document.addEventListener('DOMContentLoaded', () => { applyLang(); fetchNonce(); });
    </script>
</head>
<body>
    <div class="box">
        <!-- Sem data-i18n de proposito: SIMUT e a sigla desta frase, entao traduzir
             a legenda quebraria a correspondencia com as letras. -->
        <div class="brand"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 2951 708" fill="currentColor" role="img" aria-label="SIMUT"><g transform="translate(-29 698) scale(1 -1)"><path d="M628 198Q628 97 553 44Q478 -10 333 -10Q201 -10 125 37Q50 84 29 179L168 202Q182 147 223 123Q264 98 337 98Q488 98 488 190Q488 219 470 238Q453 257 422 270Q390 283 301 301Q224 319 193 330Q163 341 139 356Q114 371 97 392Q80 413 71 441Q61 469 61 506Q61 599 131 649Q201 698 335 698Q463 698 527 658Q591 618 610 526L470 507Q459 551 427 574Q394 596 332 596Q201 596 201 514Q201 487 215 470Q229 453 256 441Q284 429 367 411Q466 390 509 372Q552 354 577 331Q602 307 615 274Q628 241 628 198ZM704 0V688H848V0ZM1523 0V417Q1523 431 1523 445Q1523 459 1528 567Q1493 436 1477 384L1353 0H1250L1126 384L1074 567Q1080 454 1080 417V0H952V688H1145L1268 303L1278 266L1302 174L1333 284L1459 688H1651V0ZM2041 -10Q1899 -10 1823 60Q1748 129 1748 258V688H1892V269Q1892 188 1931 145Q1970 103 2045 103Q2122 103 2163 147Q2205 191 2205 274V688H2349V265Q2349 134 2268 62Q2187 -10 2041 -10ZM2757 577V0H2613V577H2391V688H2980V577Z"/></g></svg></div>
        <div class="tagline">Sistema de Monitoramento Universal e Telemetria</div>
        <form id="loginForm" onsubmit="doLogin(event)">
            <input type="text" name="user" placeholder="Username" data-i18n="log_usr" required autocomplete="off">
            <input type="password" id="passInput" name="pass" placeholder="Password" data-i18n="log_pas" required>
            <div class="chk-row"><input type="checkbox" id="chkPass" onchange="togglePass()"><label for="chkPass" data-i18n="log_show">Show password</label></div>
            <button type="submit" id="btnLogin" data-i18n="log_btn">Sign In</button>
            <div class="err" id="errMsg"></div>
            <a class="toggle-link" id="lnkChpass" onclick="setMode('chpass')" data-i18n="log_chpass_link">Change password</a>
        </form>
        <form id="chpassForm" style="display:none;" onsubmit="doChpass(event)">
            <h3 style="margin:0 0 10px 0; font-size:1.1rem;" data-i18n="log_chpass_title">Change Password</h3>
            <input type="text" name="user2" placeholder="Username" data-i18n="log_usr" required autocomplete="off">
            <input type="password" id="opInput" placeholder="Current Password" data-i18n="log_oldpass" required>
            <input type="password" id="np1" placeholder="New Password" data-i18n="log_newpass" required onkeyup="chpassStrength()">
            <div class="bar-bg"><div class="bar-fg" id="chBar"></div></div>
            <div class="req-list">
                <span id="rqLen" data-i18n="log_chpass_req_len">At least 8 characters</span>
                <span id="rqLet" data-i18n="log_chpass_req_letter">Letter</span>
                <span id="rqDig" data-i18n="log_chpass_req_digit">Digit</span>
                <span id="rqSym" data-i18n="log_chpass_req_symbol">Symbol</span>
            </div>
            <input type="password" id="np2" placeholder="Repeat New Password" data-i18n="log_newpass2" required onkeyup="chpassStrength()">
            <div class="chk-row"><input type="checkbox" id="chkPass2" onchange="togglePass2()"><label for="chkPass2" data-i18n="log_show">Show password</label></div>
            <button type="submit" id="btnChpass" data-i18n="log_chpass_btn" disabled>Save New Password</button>
            <div class="err" id="chErr"></div>
            <div class="ok-msg" id="chOk"></div>
            <a class="toggle-link" onclick="setMode('login')" data-i18n="log_chpass_back">← Back to Login</a>
        </form>
        <div class="lang-box">
            <span style="font-size:1.2rem;">🌐</span>
            <select class="lang-select" onchange="setLang(this.value)">
                <option value="en">🇺🇸 English</option>
                <option value="pt">🇧🇷 Português</option>
            </select>
        </div>
    </div>
</body>
</html>
)raw";


static const char FORCE_CHPASS_PAGE[] PROGMEM = R"raw(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate">
    <title>SIMUT - Setup</title>
    <script>if(localStorage.getItem('simut_ui_theme')==='light')document.documentElement.classList.add('light')</script>
    <style>
        :root { --bg: #0c0f13; --card: #161b22; --txt: #e9edf2; --sub: #98a6b3; --acc: #06b6d4; --dang: #ef4444; --border: #2a3340; color-scheme: dark; }
        /* Tema claro: espelha o bloco do login (sem lang.js aqui). */
        html.light { --bg: #f2f5f8; --card: #ffffff; --txt: #1b2733; --sub: #526172; --acc: #0072cd; --dang: #c93838; --border: #d9e1e8; color-scheme: light; }
        html.light input[type="text"], html.light input[type="password"] { background: #fff; border-color: var(--border); color: var(--txt); }
        html.light .box { box-shadow: 0 10px 30px rgba(27,39,51,0.12); }
        html.light button[type="submit"] { color: #fff; }
        html.light button[type="submit"]:disabled { background: #cbd5e1; color: #94a3b8; }
        html.light .lang-box select { background: #fff; color: var(--sub); }
        html.light .bar-bg { background: #e3eaf0; }
        html.light .req-list span.ok, html.light .ok-msg { color: #15803d; }
        /* margin:auto no lugar de align-items:center — este formulario passa de 700px
           de altura e centralizar por flex deixa o topo inalcancavel no celular. */
        body { background: var(--bg); color: var(--txt); font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; display: flex; min-height: 100vh; min-height: 100dvh; margin: 0; padding: 16px; box-sizing: border-box; }
        .box { background: var(--card); margin: auto; padding: clamp(20px, 5vw, 40px); border-radius: 12px; border: 1px solid var(--border); width: min(350px, 100%); box-sizing: border-box; text-align: center; box-shadow: 0 10px 30px rgba(0,0,0,0.5); }
        h2 { margin-top: 0; font-size: 1.4rem; }
        p { color: var(--sub); font-size: 0.9rem; margin-bottom: 20px; }
        input[type="password"], input[type="text"] { width: 100%; padding: 14px; margin: 10px 0; background: #000; border: 1px solid #3f3f46; color: #fff; border-radius: 8px; box-sizing: border-box; font-size: 1rem; transition: 0.2s; }
        input:focus { border-color: var(--acc); outline: none; }
        button[type="submit"] { width: 100%; padding: 14px; background: var(--acc); color: #000; font-weight: bold; border: none; border-radius: 8px; cursor: pointer; margin-top: 20px; font-size: 1.05rem; transition: 0.2s; }
        button:disabled { background: #3f3f46; color: #a1a1aa; cursor: not-allowed; }
        .bar-bg { width: 100%; height: 8px; background: #3f3f46; border-radius: 4px; margin-top: 10px; overflow: hidden; }
        .bar-fg { height: 100%; width: 0%; transition: 0.3s; background: red; }
        .req { text-align: left; font-size: 0.8rem; color: var(--sub); margin-top: 10px; }
        .chk-row { display: flex; align-items: center; gap: 8px; margin-top: 10px; justify-content: flex-start; }
        .chk-row input { width: 16px; height: 16px; accent-color: var(--acc); cursor: pointer; margin: 0;}
        .chk-row label { color: var(--sub); font-size: 0.85rem; cursor: pointer; }
        #net-toast { position:fixed;top:0;left:0;right:0;z-index:9999;text-align:center;padding:10px 20px;font-size:0.85rem;font-weight:600;transform:translateY(-100%);transition:transform .3s,opacity .3s;opacity:0;pointer-events:none; }
        #net-toast.show { transform:translateY(0);opacity:1; }
        #net-toast.warn { background:linear-gradient(135deg,#92400e,#b45309);color:#fef3c7;border-bottom:2px solid #f59e0b; }
        #net-toast.err { background:linear-gradient(135deg,#7f1d1d,#991b1b);color:#fecaca;border-bottom:2px solid #ef4444; }
        #net-toast.ok { background:linear-gradient(135deg,#064e3b,#065f46);color:#a7f3d0;border-bottom:2px solid #10b981; }
    </style>
    <script>
    /* F-LANGPACK β: dict.pt vem de GET /api/lang (servido do .lng).
     * v3.32.5: fallback inline pra chaves fcp_* faltantes no .lng do device. */
    const dictFcp = {
        pt: {
            "fcp_wel":  "Bem-vindo!",
            "fcp_msg":  "Defina uma nova senha forte para acessar o sistema.",
            "fcp_p1":   "Nova Senha",
            "fcp_p2":   "Repetir Senha",
            "fcp_req":  "Mínimo 8 caracteres, incluindo letras, números e símbolos.",
            "fcp_show": "Mostrar senhas"
        },
        en: { "fcp_btn": "Save & Login" }
    };
    fetch('/api/lang').then(r=>r.json()).then(d=>{Object.assign(dictFcp.pt,d);applyLang();}).catch(()=>{});
    function setLang(l) { localStorage.setItem('simut_lang', l); applyLang(); }
    function applyLang() { let l = localStorage.getItem('simut_lang') || 'en'; document.querySelectorAll('.lang-select').forEach(s => s.value = l); document.querySelectorAll('[data-i18n]').forEach(el => { let k = el.getAttribute('data-i18n'); if(!el.hasAttribute('data-en')) el.setAttribute('data-en', el.placeholder || el.innerHTML); let t = (l === 'en' || !dictFcp[l] || !dictFcp[l][k]) ? el.getAttribute('data-en') : dictFcp[l][k]; if(el.tagName === 'INPUT') el.placeholder = t; else el.innerHTML = t; }); }
    document.addEventListener('DOMContentLoaded', applyLang);
    function sha256(ascii){function rightRotate(value,amount){return(value>>>amount)|(value<<(32-amount));}var mathPow=Math.pow;var maxWord=mathPow(2,32);var lengthProperty='length';var i,j;var result='';var words=[];var asciiBitLength=ascii[lengthProperty]*8;var hash=sha256.h=sha256.h||[];var k=sha256.k=sha256.k||[];var primeCounter=k[lengthProperty];var isComposite={};for(var candidate=2;primeCounter<64;candidate++){if(!isComposite[candidate]){for(i=0;i<313;i+=candidate)isComposite[i]=candidate;hash[primeCounter]=(mathPow(candidate,.5)*maxWord)|0;k[primeCounter++]=(mathPow(candidate,1/3)*maxWord)|0;}}ascii+='\x80';while(ascii[lengthProperty]%64-56)ascii+='\x00';for(i=0;i<ascii[lengthProperty];i++){j=ascii.charCodeAt(i);if(j>>8)return;words[i>>2]|=j<<((3-i)%4)*8;}words[words[lengthProperty]]=((asciiBitLength/maxWord)|0);words[words[lengthProperty]]=(asciiBitLength);for(j=0;j<words[lengthProperty];){var w=words.slice(j,j+=16);var oldHash=hash;hash=hash.slice(0,8);for(i=0;i<64;i++){var w15=w[i-15],w2=w[i-2];var a=hash[0],e=hash[4];var temp1=hash[7]+(rightRotate(e,6)^rightRotate(e,11)^rightRotate(e,25))+((e&hash[5])^((~e)&hash[6]))+k[i]+(w[i]=(i<16)?w[i]:(w[i-16]+(rightRotate(w15,7)^rightRotate(w15,18)^(w15>>>3))+w[i-7]+(rightRotate(w2,17)^rightRotate(w2,19)^(w2>>>10)))|0);var temp2=(rightRotate(a,2)^rightRotate(a,13)^rightRotate(a,22))+((a&hash[1])^(a&hash[2])^(hash[1]&hash[2]));hash=[(temp1+temp2)|0].concat(hash);hash[4]=(hash[4]+temp1)|0;}for(i=0;i<8;i++)hash[i]=(hash[i]+oldHash[i])|0;}for(i=0;i<8;i++){for(j=3;j+1;j--){var b=(hash[i]>>(j*8))&255;result+=((b<16)?0:'')+b.toString(16);}}return result;}

    async function doSubmit(e) {
        e.preventDefault();
        let p1El = document.getElementById('p1');
        let p2El = document.getElementById('p2');

        // 1. Extrai para a RAM
        let val1 = p1El.value;
        let val2 = p2El.value;

        // 2. Over HTTPS send the plaintext on the encrypted channel so the
        //    server can enforce the password policy (A-5); over HTTP send the
        //    sha256 as before (nothing readable on a cleartext link).
        if (location.protocol !== 'https:') {
            if (val1.length !== 64) val1 = sha256(val1);
            if (val2.length !== 64) val2 = sha256(val2);
        }

        try {
            let fd = new URLSearchParams();
            fd.append('p1', val1);
            fd.append('p2', val2);

            let r = await fetch('/api/force_chpass', { method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body: fd.toString() });

            if(r.ok) {
                // Limpa os campos visuais e redireciona
                p1El.value = '';
                p2El.value = '';
                window.location.href = '/';
            } else {
                showToast('Error updating password.', 'err');
            }
        } catch(ex) {
            showToast('Connection error.', 'err');
        }
    }
    function showToast(msg, type, ms) { var el = document.getElementById('net-toast'); el.textContent = msg; el.className = type + ' show'; setTimeout(function() { el.className = ''; }, ms || 3000); }
    function togglePass() { let t = document.getElementById('chkPass').checked ? 'text' : 'password'; document.getElementById('p1').type = t; document.getElementById('p2').type = t; }
    </script>
</head>
<body>
    <div id="net-toast"></div>
    <div class="box">
        <h2 data-i18n="fcp_wel">Welcome!</h2>
        <p data-i18n="fcp_msg">Please set a strong new password to access the system.</p>
        <form onsubmit="doSubmit(event)">
            <input type="password" id="p1" name="p1" placeholder="New Password" data-i18n="fcp_p1" required onkeyup="checkStr()">
            <div class="bar-bg"><div class="bar-fg" id="bar"></div></div>
            <div class="req" id="req-text" data-i18n="fcp_req">At least 8 characters, including letters, numbers, and special symbols.</div>
            <input type="password" id="p2" name="p2" placeholder="Repeat Password" data-i18n="fcp_p2" required onkeyup="checkStr()">
            <div class="chk-row"><input type="checkbox" id="chkPass" onchange="togglePass()"><label for="chkPass" data-i18n="fcp_show">Show passwords</label></div>
            <button type="submit" id="btn" data-i18n="fcp_btn" disabled>Save & Login</button>
            <script>
                function checkStr() {
                    let p1 = document.getElementById('p1').value; let p2 = document.getElementById('p2').value; let s = 0;
                    if(p1.length >= 8) s += 25; if(/[A-Za-z]/.test(p1)) s += 25; if(/[0-9]/.test(p1)) s += 25; if(/[^A-Za-z0-9]/.test(p1)) s += 25;
                    let b = document.getElementById('bar'); b.style.width = s + '%';
                    if(s <= 25) b.style.background = '#ef4444'; else if(s <= 50) b.style.background = '#f59e0b'; else if(s <= 75) b.style.background = '#3b82f6'; else b.style.background = '#22c55e';
                    document.getElementById('btn').disabled = !(s === 100 && p1 === p2);
                }
            </script>
        </form>
    </div>
</body>
</html>
)raw";


static const char DASH_PAGE[] PROGMEM = R"raw(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate">
    <title>SIMUT - Dashboard</title>
    <script src="/lang.js"></script>
    <link rel="stylesheet" href="/style.css">
    <style>


        /* Dashboard Styles */
        .layout-grid { display: grid; grid-template-columns: 1fr 360px; gap: 25px; align-items: start; }
        @media(max-width: 900px) { .layout-grid { grid-template-columns: 1fr; } }
        .compact-info { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 15px; display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 15px; margin-bottom: 25px; }
        .c-item { display: flex; flex-direction: column; }
        .c-lbl { color: var(--sub); font-size: 0.75rem; text-transform: uppercase; font-weight: 700; margin-bottom: 2px; }
        .c-val { font-size: 1rem; font-weight: 600; color: var(--txt); }
        .c-sub { font-size: 0.75rem; color: var(--sub); margin-top: 2px; }
        /* No celular o piso de 180px do auto-fit nunca cabe duas vezes (precisa de
           375px e o aparelho mais largo oferece 358), entao o grid caia sempre em
           1 coluna e virava uma pilha de ~900px. Duas colunas fixas: RAM e Flash
           sao vizinhos no DOM e caem lado a lado sozinhos. */
        @media(max-width: 640px) {
            .compact-info { grid-template-columns: repeat(2, 1fr); gap: 10px 12px; padding: 12px; margin-bottom: 14px; }
            .c-lbl { font-size: 0.64rem; letter-spacing: 0.02em; }
            .c-val { font-size: 0.92rem; }
            .c-sub { font-size: 0.66rem; }
            .bar-bg { height: 4px; }
            /* a tabela de sensores rola de lado (o card ja tem overflow-x): manter
               a leitura em uma linha por celula em vez de empilhar caracteres */
            #tab td, thead th { white-space: nowrap; }
        }
        .bar-bg { background: var(--track); height: 6px; border-radius: 3px; overflow: hidden; margin-top: 4px; width: 100%; }
        .bar-fg { background: var(--acc); height: 100%; border-radius: 3px; transition: width 0.5s ease; }
        .bar-fg.warn { background: #f59e0b; } .bar-fg.crit { background: var(--dang); }
        table { width: 100%; border-collapse: collapse; }
        th, td { padding: 14px; text-align: left; border-bottom: 1px solid var(--border); }
        th { color: var(--sub); font-size: 0.85rem; text-transform: uppercase; }
        .dot { height: 10px; width: 10px; background: var(--acc); border-radius: 50%; display: inline-block; }
        .err { background: var(--dang); }
        .display-box { background: var(--card); border: 1px solid var(--border); padding: 20px; border-radius: 12px; display: flex; flex-direction: column; align-items: center; }
        button, select { background: var(--bg); color: var(--txt); border: 1px solid var(--border); padding: 8px 12px; border-radius: 6px; cursor: pointer; transition: 0.2s;}
        button:hover { background: var(--acc); color: black; border-color: var(--acc); }
        @keyframes spin { 100% { transform: rotate(360deg); } }
    </style>
    <script>
        /* window.t/applyLang/setLang/showToast/fetchSafe vem de /lang.js */
        document.addEventListener('DOMContentLoaded', () => { setTimeout(applyLang, 50); setTimeout(() => { let activeTab = document.querySelector('.nav a.active'); if (activeTab) { activeTab.scrollIntoView({ behavior: 'smooth', block: 'nearest', inline: 'center' }); } }, 100); }); window.updateLang = applyLang;

    </script>
</head>
<body>
    <script>installTopbar('nav_dash', 'Dashboard')</script>

    <div class="container">
        <div class="layout-grid">
            <div class="main-content">
                <div class="compact-info">
                    <div class="c-item"><div class="c-lbl" data-i18n="dash_dt">System Date & Time</div><div class="c-val" id="sys-time">--/--/---- --:--:--</div><div class="c-sub" id="ntp-stat" data-i18n="dash_ntp_wait">Waiting NTP...</div></div>
                    <div class="c-item"><div class="c-lbl" data-i18n="dash_up">System Uptime</div><div class="c-val" id="up">--d --h --m --s</div></div>
                    <div class="c-item"><div class="c-lbl" data-i18n="dash_wifi">WiFi Signal</div><div class="c-val" id="rssi">-- dBm</div><div class="c-sub" id="rssi-mm">min --/-- max</div></div>
                    <div class="c-item"><div class="c-lbl" data-i18n="dash_pend">Pending Records</div><div class="c-val" id="pending">-- pkts</div><div class="c-sub" id="pending-sub">Waiting telemetry sync</div></div>
                    <div class="c-item">
                        <div style="display:flex; justify-content:space-between;"><div class="c-lbl" data-i18n="dash_ram">RAM Usage</div><div class="c-val" style="font-size:0.85rem;" id="ram-pct">--%</div></div>
                        <div class="bar-bg"><div class="bar-fg" id="ram-bar" style="width: 0%"></div></div>
                        <div class="c-sub" id="ram-txt" style="text-align:right">-- KB / -- KB</div>
                    </div>
                    <div class="c-item">
                        <div style="display:flex; justify-content:space-between;"><div class="c-lbl" data-i18n="dash_fs">Flash Storage</div><div class="c-val" style="font-size:0.85rem;" id="fs-pct">--%</div></div>
                        <div class="bar-bg"><div class="bar-fg" id="fs-bar" style="width: 0%"></div></div>
                        <div class="c-sub" id="fs-txt" style="text-align:right">-- KB / -- KB</div>
                    </div>
                </div>
                <div class="compact-info">
                    <div class="c-item"><div class="c-lbl" data-i18n="dash_metr_lblk">Largest Block</div><div class="c-val" id="m-lblk">-- KB</div><div class="c-sub" id="m-lblk-min" data-i18n-sub="dash_metr_min">min --</div></div>
                    <div class="c-item"><div class="c-lbl" data-i18n="dash_metr_hmin">Heap Min Seen</div><div class="c-val" id="m-hmin">-- KB</div><div class="c-sub" data-i18n="dash_metr_hmin_sub">lowest free</div></div>
                    <div class="c-item"><div class="c-lbl" data-i18n="dash_metr_net">Net Reconnects</div><div class="c-val" id="m-net">-- / --</div><div class="c-sub" data-i18n="dash_metr_net_sub">wifi / mqtt</div></div>
                    <div class="c-item"><div class="c-lbl" data-i18n="dash_metr_tel">Telemetry Sent</div><div class="c-val" id="m-tel">-- ok</div><div class="c-sub" id="m-tel-sub">-- fail · -- retry</div></div>
                    <div class="c-item"><div class="c-lbl" data-i18n="dash_metr_data">Telemetry Data</div><div class="c-val" id="m-data">-- KB</div><div class="c-sub" id="m-lat">last -- ms</div></div>
                    <div class="c-item"><div class="c-lbl" data-i18n="dash_metr_snr">Sensor Reads</div><div class="c-val" id="m-snr">-- ok</div><div class="c-sub" id="m-snr-err">-- err</div></div>
                    <div class="c-item"><div class="c-lbl" data-i18n="dash_metr_cfg">Config Saves</div><div class="c-val" id="m-cfg">--</div><div class="c-sub" data-i18n="dash_metr_cfg_sub">to flash</div></div>
                </div>
                <div class="card" style="padding:0; overflow-x:auto;">
                    <table>
                        <thead>
                            <tr>
                                <th style="width: 40px; text-align: center;" data-i18n="dash_stat">Status</th>
                                <th style="width: 60px;" data-i18n="dash_gpio">SLOT</th>
                                <th style="width: 70px;" data-i18n="dash_type">Type</th>
                                <th data-i18n="dash_id">ID Sensor</th>
                                <th data-i18n="dash_read">Reading</th>
                                <th data-i18n="dash_name">Sensor Name</th>
                            </tr>
                        </thead>
                        <tbody id="tab">
                            <tr><td colspan="6" style="text-align:center;padding:20px" data-i18n="dash_load">Loading sensors...</td></tr>
                        </tbody>
                    </table>
                </div>
            </div>
            <div class="side-content">
                <div class="display-box">
                    <h2 style="align-self: flex-start; margin-top:0; font-size:1.2rem;" data-i18n="dash_disp">Display Capture</h2>
                    <div id="theme-preview-container" style="position: relative; display: inline-block; width:100%;">
                        <div id="placeholder-box" style="width: 100%; aspect-ratio: 4/3; border: 2px dashed #3f3f46; border-radius: 6px; display: flex; align-items: center; justify-content: center; color: var(--sub); background: #000; font-size: 0.9rem;" data-i18n="dash_disp_msg">
                            Click Capture to view screen
                        </div>
                        <img id="theme-preview-img" src="" alt="Screen Capture" style="width: 100%; aspect-ratio: 4/3; object-fit: contain; background: #000; border: 4px solid #3f3f46; border-radius: 6px; display: none;">
                        <div id="loading-overlay" style="display: none; position: absolute; top: 0; left: 0; right: 0; bottom: 0; background: rgba(0,0,0,0.75); border-radius: 6px; flex-direction: column; justify-content: center; align-items: center; color: #fff;">
                            <span data-i18n="dash_disp_cap">Capturing Display...</span>
                        </div>
                    </div>
                    <div style="margin-top:15px; display: flex; gap: 10px; width: 100%; justify-content: center;">
                        <select id="themeSel" onchange="saveTheme()"><option value="0">Loading...</option></select>
                        <button id="capBtn" onclick="captureScreen()" data-i18n="dash_disp_btn">📷 Capture Screen</button>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <script>
        let sysData = null; function fmt(v){return v < 10 ? '0' + v : v;}

        async function fetchLoop() {
            try {
                const res = await fetchSafe('/api/status');
                if (!res.ok) throw new Error("HTTP " + res.status);
                const text = await res.text();
                sysData = JSON.parse(text); const d = sysData.sys;

                if (d.theme !== undefined && document.activeElement.id !== "themeSel") document.getElementById('themeSel').value = d.theme;

                let s = Math.floor(d.uptime / 1000); let days = Math.floor(s / 86400); s %= 86400; let hrs = Math.floor(s / 3600); s %= 3600; let mins = Math.floor(s / 60); let secs = s % 60;
                let upStr = (days > 0 ? days + "d " : "") + (hrs > 0 || days > 0 ? fmt(hrs) + "h " : "") + fmt(mins) + "m " + fmt(secs) + "s";
                document.getElementById('up').innerText = upStr;

                let st = document.getElementById('ntp-stat');
                if (d.ntp === 1) {
                    let dt = new Date(d.time * 1000);
                    document.getElementById('sys-time').innerText = fmt(dt.getDate()) + "/" + fmt(dt.getMonth()+1) + "/" + dt.getFullYear() + " " + fmt(dt.getHours()) + ":" + fmt(dt.getMinutes()) + ":" + fmt(dt.getSeconds());
                    st.innerText = window.t('dash_ntp_ok', "NTP Synced"); st.style.color = "var(--acc)";
                } else {
                    document.getElementById('sys-time').innerText = window.t('dash_ntp_fail', "Not Synced");
                    st.innerText = window.t('dash_ntp_conn', "Waiting connection..."); st.style.color = "var(--dang)";
                }

                document.getElementById('rssi').innerText = d.rssi + ' dBm';
                let pend = document.getElementById('pending');
                if (d.pending === -1) pend.innerText = window.t('dash_eval', "Evaluating..."); else pend.innerText = d.pending + " pkts";
                /* The sub-line used to be static markup carrying data-i18n, so
                   applyLang wrote "Waiting telemetry sync" once at load and
                   nothing ever revisited it — it read "waiting" forever, including
                   with the counter at zero. It is driven from the poll now, and
                   the data-i18n was dropped so applyLang cannot overwrite the
                   state on a language switch. */
                let psub = document.getElementById('pending-sub');
                if (psub) {
                    if (d.pending === -1)   psub.innerText = window.t('dash_tel_na',   'Telemetry unavailable');
                    else if (d.tel === 0)   psub.innerText = window.t('dash_tel_off',  'Telemetry disabled');
                    else if (d.pending === 0) psub.innerText = window.t('dash_tel_ok', 'Synchronized');
                    else                    psub.innerText = window.t('dash_tel_wait', 'Waiting telemetry sync');
                }

                const m = sysData.metr;
                if (m) {
                    let rmm = document.getElementById('rssi-mm');
                    if (rmm && m.rmn !== undefined && m.rmx !== undefined) rmm.innerText = window.t('dash_metr_min','min') + ' ' + m.rmn + ' / ' + m.rmx + ' ' + window.t('dash_metr_max','max');
                    const kb = (n) => (n / 1024).toFixed(1) + ' KB';
                    const fmtN = (n) => { if (n >= 1e6) return (n/1e6).toFixed(2)+'M'; if (n >= 1e3) return (n/1e3).toFixed(1)+'k'; return String(n); };
                    let el;
                    if ((el=document.getElementById('m-lblk'))) el.innerText = kb(m.lb||0);
                    if ((el=document.getElementById('m-lblk-min'))) el.innerText = window.t('dash_metr_min','min') + ' ' + kb(m.lbm||0);
                    if ((el=document.getElementById('m-hmin'))) el.innerText = kb(m.hm||0);
                    if ((el=document.getElementById('m-net'))) el.innerText = (m.wf||0) + ' / ' + (m.mq||0);
                    if ((el=document.getElementById('m-tel'))) el.innerText = fmtN(m.ts||0) + ' ' + window.t('dash_metr_ok','ok');
                    if ((el=document.getElementById('m-tel-sub'))) el.innerText = (m.tf||0) + ' ' + window.t('dash_metr_fail','fail') + ' · ' + (m.tr||0) + ' ' + window.t('dash_metr_retry','retry');
                    if ((el=document.getElementById('m-data'))) el.innerText = kb(m.tb||0);
                    if ((el=document.getElementById('m-lat'))) el.innerText = window.t('dash_metr_last','last') + ' ' + (m.tl||0) + ' ms';
                    if ((el=document.getElementById('m-snr'))) el.innerText = fmtN(m.so||0) + ' ' + window.t('dash_metr_ok','ok');
                    if ((el=document.getElementById('m-snr-err'))) el.innerText = (m.se||0) + ' ' + window.t('dash_metr_err','err');
                    if ((el=document.getElementById('m-cfg'))) el.innerText = String(m.cs||0);
                }

                if (d.heap_t > 0) {
                    let ramPct = Math.round(((d.heap_t - d.heap_f) / d.heap_t) * 100);
                    document.getElementById('ram-pct').innerText = ramPct + "%";
                    document.getElementById('ram-txt').innerText = (d.heap_f / 1024).toFixed(1) + " KB " + window.t('dash_free','Free') + " / " + (d.heap_t / 1024).toFixed(1) + " KB " + window.t('dash_tot','Total');
                    let rBar = document.getElementById('ram-bar'); rBar.style.width = ramPct + "%"; rBar.className = "bar-fg " + (ramPct > 85 ? "crit" : (ramPct > 70 ? "warn" : ""));
                }
                if (d.fs_t > 0) {
                    let fsPct = Math.round((d.fs_u / d.fs_t) * 100);
                    document.getElementById('fs-pct').innerText = fsPct + "%";
                    document.getElementById('fs-txt').innerText = ((d.fs_t - d.fs_u) / 1024).toFixed(1) + " KB " + window.t('dash_free','Free') + " / " + (d.fs_t / 1024).toFixed(1) + " KB " + window.t('dash_tot','Total');
                    let fBar = document.getElementById('fs-bar'); fBar.style.width = fsPct + "%"; fBar.className = "bar-fg " + (fsPct > 85 ? "crit" : (fsPct > 70 ? "warn" : ""));
                }

                let tabHtml = '';
                if (sysData.sensors && sysData.sensors.length > 0) {
                    sysData.sensors.forEach((sn) => {
                        let v = (sn.val === 'Error' || sn.val === '--') ? sn.val : parseFloat(sn.val).toFixed(2) + ' ºC';
                        if(sn.hum) v += ' | ' + parseFloat(sn.hum).toFixed(1) + '%';
                        if(sn.press) v += ' | ' + parseFloat(sn.press).toFixed(1) + ' hPa';
                        const typeLabel = sn.type || '?';
                        const typeCls = sn.type === 'DHT22' ? 'color:var(--warn)' : 'color:var(--ok)';
                        const pinInfo = (sn.pc && sn.pr) ? `<span style="font-size:0.7rem;color:var(--sub)"> ⚡${sn.pc}p ${sn.pr}</span>` : '';
                        tabHtml += `<tr><td style="text-align:center;"><span class="dot ${sn.val === 'Error' ? 'err' : ''}"></span></td><td style="color:var(--sub)">${sn.slot}</td><td style="${typeCls}; font-size:0.85rem; font-weight:600;">${typeLabel}${pinInfo}</td><td style="font-family:monospace; color:var(--acc); font-weight:600;">${escHtml(sn.id)}</td><td style="color:var(--txt); font-weight:600;">${escHtml(v)}</td><td style="font-weight:600; color:var(--txt)">${escHtml(sn.name)}</td></tr>`;
                    });
                    document.getElementById('tab').innerHTML = tabHtml;
                } else { document.getElementById('tab').innerHTML = `<tr><td colspan="6" style="text-align:center;color:var(--sub)">No active sensors.</td></tr>`; }
            } catch(e) { document.getElementById('tab').innerHTML = `<tr><td colspan="6" style="color:var(--dang);text-align:center;font-weight:bold;padding:20px;">Connection Error</td></tr>`; }
        }

        async function loadThemes() {
            try {
                let res = await fetchSafe('/api/themes');
                let text = await res.text(); // Lemos como texto primeiro para evitar falha silenciosa
                let themes = JSON.parse(text); // Tentamos converter
                let html = '';
                themes.forEach(t => html += `<option value="${escHtml(t.id)}">${escHtml(t.name)}</option>`);
                document.getElementById('themeSel').innerHTML = html;
            } catch(e){
                // Se o JSON vier quebrado do C++, o botão mostrará o erro na hora!
                document.getElementById('themeSel').innerHTML = `<option value="0">Err: ${e.message.substring(0,20)}</option>`;
            }
        }

        async function saveTheme() {
            const idx = document.getElementById('themeSel').value;
            const params = new URLSearchParams(); params.append('theme', idx);
            await fetchSafe('/api/save_sys', { method:'POST', body: params });
        }

        function captureScreen() {
            let btn = document.getElementById('capBtn'); if(!btn) return;
            let origText = btn.innerText; btn.innerText = "⏳..."; btn.disabled = true;
            document.getElementById('placeholder-box').style.display = 'none';
            let img = document.getElementById('theme-preview-img');
            img.src = '';
            img.style.display = 'block';
            document.getElementById('loading-overlay').style.display = 'flex';
            document.getElementById('loading-overlay').innerHTML = '<span data-i18n="dash_disp_cap">Capturing Display...</span>';
            img.onload = () => { document.getElementById('loading-overlay').style.display = 'none'; btn.innerText = origText; btn.disabled = false; };
            img.onerror = () => { document.getElementById('loading-overlay').innerHTML = "<span style='color:#f87171'>Read Failed</span>"; setTimeout(() => { document.getElementById('loading-overlay').style.display = 'none'; }, 2000); btn.innerText = origText; btn.disabled = false; };
            img.src = '/api/screenshot?t=' + new Date().getTime();
        }

        window.onLangChange = function() { fetchLoop(); };


        /* Sensor provisioning and calibration moved to /config (slot editor).
         * The dashboard is status-only. */

        document.addEventListener('DOMContentLoaded', () => { loadThemes(); fetchLoop(); setInterval(fetchLoop, 3000); });
    </script>
</body>
</html>
)raw";


static const char HIST_PAGE[] PROGMEM = R"raw(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate">
    <title>SIMUT - History</title>
    <script src="/lang.js"></script>
    <link rel="stylesheet" href="/style.css">
    <!-- h5g: o renderizador do grafico. Vive AQUI, embutido, e nao num CDN.
         A pagina prometia funcionar sem internet e nao funcionava: sem rede a
         chamada `new Chart(...)` lancava ReferenceError, o catch do carregador
         a engolia e o usuario lia "Connection lost." com o aparelho na LAN e o
         .h5 ja baixado. A tag anterior tambem nao fixava versao (`npm/chart.js`
         = ultima major) nem trazia integrity, num documento que carrega cookie
         de sessao.
         Bancada, casos congelados e portoes: scratchpad/h5g_20260818/. -->
    <script>
/* h5g — renderizador do grafico da /history do SIMUT.
 *
 * Substitui o Chart.js sob a MESMA assinatura: renderChart() monta a mesma
 * configuracao e chama `new Chart(ctx, cfg)`. Trocar o motor nao muda a pagina.
 *
 * O que este arquivo NAO tem, de proposito: animacao, resolucao de opcoes em
 * cascata, outros tipos de grafico, interpolacao spline, escala de tempo,
 * plugins genericos, parser de cor. A pagina nao usa nada disso, e e ai que
 * mora a maior parte dos 43,5 KB de nucleo do Chart.js.
 *
 * Superficie que a pagina e o plugin da banda consomem, e que portanto e
 * contrato:
 *     new Chart(ctx, cfg) · destroy()
 *     chart.ctx · chart.chartArea · chart.data.datasets
 *     chart.scales[id].getPixelForValue(v) · chart.isDatasetVisible(i)
 */
(function (global) {
'use strict';

/* ---- fonte: os mesmos padroes do Chart.js, para o desenho nao mudar de
 * tamanho ao trocar de motor. ---- */
var FONT = "'Helvetica Neue', Helvetica, Arial, sans-serif";
var TICK_PX = 12, TITLE_PX = 10;
var PAD = 6;            /* respiro entre area do grafico e rotulos */
/* Passo minimo entre ticks do Y = 1,4x a altura do rotulo. Nao e numero
 * escolhido a dedo: e a regra da referencia, e na pratica quem manda quase
 * sempre e o teto de 11 ticks. Um passo fixo (28 px) casava com o grafico alto
 * e errava no baixo — no caso de 280 px de altura ele dava 5 ticks onde a
 * referencia da 9, e a regressao so apareceu quando a legenda comeu 21 px do
 * topo. Densidade nao e constante: e funcao do espaco. */
var Y_PITCH = TICK_PX * 1.4;
var TICK_LEN = 0;       /* a pagina nao desenha marcas para fora do eixo */
var BAND_IN_RANGE = true;   /* ver fromData() */

/* Legenda: a marca e um RETANGULO com a cor e o tracejado da serie, nao um
 * segmento de reta — e assim que a referencia desenha, e e o tracejado dentro
 * dela que distingue umidade de pressao para quem nao separa as cores.
 * 40x11 mais 10 de respiro reproduzem os 20,8 px que a referencia reserva. */
var LEG_PX = 11, LEG_BOX_W = 40, LEG_BOX_H = 11, LEG_PAD = 10, LEG_GAP = 5;

/* Tooltip: os padroes do Chart.js, que a pagina nao sobrescreve. */
var TIP_BG = 'rgba(0,0,0,0.8)', TIP_FG = '#fff', TIP_PX = 12, TIP_PAD = 6, TIP_R = 6;
/* Distancia maxima, em pixels, entre o cursor e a amostra lida. Os dados
 * chegam decimados a ~1 ponto por pixel, entao sobre dado a amostra mais
 * proxima esta sempre a menos de 1 px; 20 px so e ultrapassado dentro de uma
 * LACUNA. Sem esse corte a caixa mostrava, com o cursor no meio do buraco, uma
 * leitura de tres horas antes — e a linha vertical ia junto, o que se le como
 * "existe dado aqui". A referencia tambem nao mostra nada na lacuna; a
 * diferenca e que aqui isso e decisao, e nao efeito colateral de uma
 * coordenada NaN. */
var TIP_MAX_DIST = 20;

/* Rotulos do eixo X: retos enquanto couberem, girados quando nao couberem.
 * A alternativa era afinar os ticks, e ela custava a GRADE junto: numa tela de
 * 375 px sobravam 3 linhas verticais em vez de 7, e localizar um evento no
 * tempo virava estimar entre marcas de 12 horas. Girar custa ~17 px de altura
 * (8% do grafico num celular) e devolve a referencia temporal inteira. */
var X_ROT_DEG = 45, SIN45 = 0.70710678, XT_MAX = 99;

/* ---- passo "bonito": 1, 2 ou 5 vezes uma potencia de 10. E o unico
 * algoritmo de verdade do renderizador. ---- */
function niceStep(raw) {
    if (!(raw > 0) || !isFinite(raw)) return 1;
    var mag = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10));
    var f = raw / mag;
    return (f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10) * mag;
}

/* Ticks dentro de [min,max]. Com `step` dado (eixo X das faixas fixas) o passo
 * e imposto; sem ele, escolhido. Nunca devolve menos de 2. */
function makeTicks(min, max, maxCount, step) {
    if (!(max > min)) return [min];
    var s = step || niceStep((max - min) / Math.max(1, maxCount - 1));
    var out = [];
    var first = Math.ceil(min / s) * s;
    /* O laco anda por indice e nao por soma acumulada: somar `s` repetidas
     * vezes acumula erro de ponto flutuante e o ultimo tick sai deslocado. */
    for (var i = 0; ; i++) {
        var v = first + i * s;
        if (v > max + s * 1e-9) break;
        out.push(v);
        if (out.length > 1000) break;              /* trava de seguranca */
    }
    return out.length ? out : [min, max];
}

/* Quantas casas decimais o passo exige. Sem isto, 0.1+0.2 vira "0.30000000004"
 * no rotulo. */
function decimalsFor(step) {
    if (!isFinite(step) || step <= 0) return 0;
    var d = Math.ceil(-Math.log(step) / Math.LN10);
    return d > 0 ? Math.min(d, 10) : 0;
}

function fmtNumber(v, dec) {
    /* toLocaleString sem locale segue o idioma da pagina, que e o que o
     * Chart.js tambem faz — os rotulos precisam combinar com o resto da UI. */
    return v.toLocaleString(undefined, { minimumFractionDigits: dec, maximumFractionDigits: dec });
}

/* ---- escala linear ---- */
function Scale(id, opt, horizontal) {
    this.id = id;
    this.opt = opt || {};
    this.horizontal = !!horizontal;
    this.display = this.opt.display !== false;
    this.min = 0; this.max = 1;
    this.ticks = [];
    this.labels = [];
    this.a = 0; this.b = 0;        /* pixel = a * valor + b */
}

Scale.prototype.fromData = function (datasets, visible) {
    /* Limites explicitos vencem os dados: a janela do X e o periodo pedido,
     * mesmo quando nao ha registro nenhum nele. */
    var lo = Infinity, hi = -Infinity;
    for (var i = 0; i < datasets.length; i++) {
        if (!visible[i]) continue;
        var ds = datasets[i];
        if (!this.horizontal && ds.yAxisID !== this.id) continue;
        var arrs = [ds.data];
        /* A banda ENTRA no intervalo do eixo. O Chart.js a deixava de fora —
         * la ela e um plugin, que a escala nao enxerga — e o pico saia cortado
         * pela borda da area. Numa cadeia fria o maximo da banda E a excursao:
         * o instante em que a temperatura cruzou o limite. Corta-lo esconde
         * justamente o dado que motiva o registrador.
         *
         * O contra-argumento era que os badges MAX/MIN acima do grafico ja dao
         * o numero. Mas eles so aparecem com UM sensor selecionado (veja
         * `oneSensor` na montagem): com dois ou mais, os badges somem e a banda
         * ficava cortada — o extremo desaparecia por completo. Esse e o caso
         * que decidiu.
         *
         * O preco esta pago com consciencia: o eixo abre e a linha media perde
         * cerca de um quarto da resolucao vertical. */
        if (BAND_IN_RANGE && ds._h5BandLo) { arrs.push(ds._h5BandLo); arrs.push(ds._h5BandHi); }
        for (var k = 0; k < arrs.length; k++) {
            var a = arrs[k];
            for (var j = 0; j < a.length; j++) {
                var p = a[j];
                if (!p) continue;
                var v = this.horizontal ? p.x : p.y;
                if (v === null || v === undefined || !isFinite(v)) continue;
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
        }
    }
    var omin = this.opt.min, omax = this.opt.max;
    if (omin !== null && omin !== undefined) lo = omin;
    if (omax !== null && omax !== undefined) hi = omax;
    if (!isFinite(lo) || !isFinite(hi)) { lo = 0; hi = 1; }
    if (lo === hi) { lo -= 0.5; hi += 0.5; }        /* serie constante */
    /* dataMin/dataMax e a fonte da verdade para buildTicks; min/max sao o
     * resultado ja esticado e mudam a cada passada do layout. */
    this.dataMin = lo; this.dataMax = hi;
    this.min = lo; this.max = hi;
};

/* Calcula ticks e rotulos. maxCount vem do espaco disponivel.
 *
 * Parte SEMPRE do intervalo dos dados, nunca do intervalo ja esticado pela
 * chamada anterior. O layout obriga a chamar isto duas vezes — mede rotulo,
 * decide a calha, remede — e realimentar o proprio resultado degradava o passo
 * a cada passada: 0,2 virava 0,5 e depois 1,0, sem nada no desenho denunciando
 * que o eixo tinha piorado sozinho. */
Scale.prototype.buildTicks = function (maxCount, labelFn) {
    var t = this.opt.ticks || {};
    var limit = Math.min(t.maxTicksLimit || 11, Math.max(2, maxCount));
    var lo = this.dataMin, hi = this.dataMax;
    var step = t.stepSize || niceStep((hi - lo) / Math.max(1, limit - 1));

    /* O eixo Y estica ate o tick cheio para que a grade case com a borda da
     * area. O X nao: a janela dele e o periodo pedido, e esticar mentiria
     * sobre o intervalo consultado. */
    if (!this.horizontal) {
        lo = Math.floor(lo / step) * step;
        hi = Math.ceil(hi / step) * step;
    }
    this.min = lo; this.max = hi;

    var vals = makeTicks(lo, hi, limit, step);

    /* autoSkip so vale quando o passo foi IMPOSTO de fora (eixo X das faixas
     * fixas), porque ai o numero de ticks nao passou por nenhum limite. Quando
     * o passo foi escolhido aqui, ele ja foi escolhido para caber — e esticar
     * o eixo ate o tick cheio acrescenta ate um tick de cada lado, o que
     * bastava para estourar o limite por 1 e o autoSkip entao pulava de 2 em 2,
     * dobrando o passo. O eixo saia com metade da resolucao pedida. */
    if (t.stepSize && t.autoSkip !== false && vals.length > limit) {
        var every = Math.ceil(vals.length / limit);
        var kept = [];
        for (var i = 0; i < vals.length; i += every) kept.push(vals[i]);
        vals = kept;
    }

    this.ticks = vals;
    var dec = decimalsFor(step);
    this.labels = vals.map(function (v) {
        return labelFn ? String(labelFn(v)) : fmtNumber(v, dec);
    });
};

Scale.prototype.setRange = function (p0, p1) {
    /* p0 = pixel do minimo, p1 = pixel do maximo. No Y vem invertido. */
    var span = (this.max - this.min) || 1;
    this.a = (p1 - p0) / span;
    this.b = p0 - this.a * this.min;
};

Scale.prototype.getPixelForValue = function (v) { return this.a * v + this.b; };
Scale.prototype.getValueForPixel = function (p) { return this.a ? (p - this.b) / this.a : this.min; };

/* ---- o grafico ---- */
function H5G(ctx, config) {
    this.ctx = ctx.canvas ? ctx : ctx.getContext('2d');
    this.canvas = this.ctx.canvas;
    this.config = config || {};
    this.data = this.config.data || { datasets: [] };
    this.options = this.config.options || {};
    this.plugins = this.config.plugins || [];
    this.scales = {};
    this.chartArea = null;
    this._hidden = {};
    this._hover = null;
    this._legend = null;
    this._destroyed = false;

    var self = this;
    this._onResize = function () { if (!self._destroyed) self.draw(); };
    if (global.ResizeObserver) {
        this._ro = new ResizeObserver(this._onResize);
        this._ro.observe(this.canvas.parentNode || this.canvas);
    }
    this._bind();
    this.draw();
}

H5G.prototype.isDatasetVisible = function (i) { return !this._hidden[i]; };

H5G.prototype.destroy = function () {
    this._destroyed = true;
    this._unbind();
    if (this._ro) { this._ro.disconnect(); this._ro = null; }
    var c = this.canvas;
    if (c) this.ctx.clearRect(0, 0, c.width, c.height);
};

/* Ajusta o buffer do canvas ao tamanho CSS do pai, com o fator da tela.
 * Sem isto a linha sai borrada em tela retina e o grafico nao acompanha a
 * caixa, que na pagina o usuario pode arrastar. */
H5G.prototype._resize = function () {
    var c = this.canvas, host = c.parentNode;
    var dpr = global.devicePixelRatio || 1;
    var w = Math.floor((host ? host.clientWidth : c.clientWidth) || c.width);
    var h = Math.floor((host ? host.clientHeight : c.clientHeight) || c.height);
    if (w <= 0 || h <= 0) { w = c.width || 300; h = c.height || 150; dpr = 1; }
    c.style.width = w + 'px';
    c.style.height = h + 'px';
    var bw = Math.round(w * dpr), bh = Math.round(h * dpr);
    if (c.width !== bw || c.height !== bh) { c.width = bw; c.height = bh; }
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    this.width = w; this.height = h;
};

H5G.prototype.draw = function () {
    if (this._destroyed) return;
    this._resize();
    var ctx = this.ctx, W = this.width, H = this.height;
    ctx.clearRect(0, 0, W, H);

    var so = this.options.scales || {};
    var visible = this.data.datasets.map(function (_, i) { return this.isDatasetVisible(i); }, this);

    /* 1. escalas */
    var xs = new Scale('x', so.x, true);
    xs.fromData(this.data.datasets, visible);
    this.scales = { x: xs };
    var yIds = ['y', 'y1', 'y2'], ys = [];
    for (var i = 0; i < yIds.length; i++) {
        var s = new Scale(yIds[i], so[yIds[i]], false);
        s.fromData(this.data.datasets, visible);
        this.scales[yIds[i]] = s;
        if (s.display) ys.push(s);
    }

    /* 2. ticks provisorios, para medir os rotulos e saber quanto de calha
     *    cada eixo precisa. A largura do rotulo depende do tick, e a
     *    quantidade de ticks depende do espaco: uma passada so nao fecha. */
    var xLabelFn = (so.x && so.x.ticks && so.x.ticks.callback) || null;
    ctx.font = TICK_PX + 'px ' + FONT;
    for (i = 0; i < ys.length; i++) ys[i].buildTicks(Math.max(2, Math.floor(H / Y_PITCH)), null);

    /* Cada eixo Y ocupa uma calha; a ordem de empilhamento e a de declaracao,
     * e dentro dela o titulo fica por fora e os rotulos por dentro. Guardo em
     * _edge onde o texto se alinha e em _titleX o centro do titulo girado,
     * porque o desenho acontece depois, ja sem esta contabilidade a mao. */
    function labelW(s) {
        var w = 0;
        for (var k = 0; k < s.labels.length; k++) w = Math.max(w, ctx.measureText(s.labels[k]).width);
        return Math.ceil(w);
    }
    var lefts = [], rights = [], lw, tw, s2;
    for (i = 0; i < ys.length; i++) {
        s2 = ys[i];
        var m = { s: s2,
                  lw: labelW(s2),
                  tw: (s2.opt.title && s2.opt.title.display) ? TITLE_PX + 8 : 0 };
        if ((s2.opt.position || 'left') === 'left') lefts.push(m); else rights.push(m);
    }

    /* Esquerda: do canto para dentro, titulo por fora e rotulos por dentro. */
    var lx = PAD;
    for (i = 0; i < lefts.length; i++) {
        lefts[i].s._titleX = lx + TITLE_PX * 0.75;
        lefts[i].s._edge = lx + lefts[i].tw + lefts[i].lw;      /* textAlign right */
        lx += lefts[i].tw + lefts[i].lw + PAD;
    }

    /* Direita: o PRIMEIRO eixo declarado fica junto do grafico e os seguintes
     * se afastam. Empilhar a partir da borda do canvas invertia a ordem — %RH
     * ia para fora e hPa para dentro — e trocava a leitura de quem so olha a
     * distancia entre a serie e a sua regua. Isso exige somar a largura toda
     * antes de posicionar o primeiro. */
    var rightTotal = PAD;
    for (i = 0; i < rights.length; i++) rightTotal += rights[i].lw + rights[i].tw + PAD;
    var rx = W - rightTotal + PAD;
    for (i = 0; i < rights.length; i++) {
        rights[i].s._edge = rx;                                 /* textAlign left */
        rights[i].s._titleX = rx + rights[i].lw + TITLE_PX * 0.75;
        rx += rights[i].lw + rights[i].tw + PAD;
    }

    var left = lx, right = rightTotal;
    var bottom = PAD + TICK_PX + PAD;
    var top = PAD + TICK_PX * 0.5;   /* meia linha p/ o rotulo do topo do Y nao cortar */
    this._legend = this._layoutLegend(visible);
    top += this._legend ? this._legend.h : 0;

    /* Passada provisoria do X. Serve a duas contas que dependem uma da outra:
     * quanto reservar nas bordas, e se os rotulos cabem retos. O primeiro e o
     * ultimo ficam centrados num tick que mora na borda da area, entao metade
     * do texto cai fora do canvas e sai cortado ("01:0"). */
    var xs0 = new Scale('x', so.x, true);
    xs0.fromData(this.data.datasets, visible);
    xs0.buildTicks(XT_MAX, xLabelFn);      /* sem afinar: so o maxTicksLimit vale */
    var xw = 0, halfFirst = 0, halfLast = 0;
    for (i = 0; i < xs0.labels.length; i++) {
        xw = Math.max(xw, ctx.measureText(xs0.labels[i]).width);
    }
    if (xs0.labels.length) {
        halfFirst = ctx.measureText(xs0.labels[0]).width / 2;
        halfLast = ctx.measureText(xs0.labels[xs0.labels.length - 1]).width / 2;
    }
    /* Cabem retos? UMA medida decide, e a mesma alimenta o limite de ticks la
     * embaixo. Antes eram duas provas independentes — esta, que media o rotulo,
     * e um divisor fixo de 60 px no limite de ticks — e elas discordavam: a
     * primeira dizia "cabe reto" enquanto a segunda afinava de 7 ticks para 3.
     * O eixo saia sem rotacao E sem grade, o pior dos dois mundos. */
    var xPitch = xw + 8;                       /* vao minimo entre rotulos retos */
    var xFit = Math.floor((W - left - right) / xPitch) + 1;
    this._xRot = (xs0.ticks.length > xFit) ? X_ROT_DEG : 0;

    if (this._xRot) {
        /* Girado, o rotulo termina no tick e desce para a esquerda: come altura
         * embaixo e transborda a esquerda do primeiro tick. */
        bottom = PAD + Math.ceil((xw + TICK_PX) * SIN45) + PAD;
        left = Math.max(left, Math.ceil(PAD + xw * SIN45));
        right = Math.max(right, PAD);
    } else {
        left = Math.max(left, Math.ceil(PAD + halfFirst));
        right = Math.max(right, Math.ceil(PAD + halfLast));
    }

    var area = { left: left, top: top, right: W - right, bottom: H - bottom };
    if (area.right <= area.left) area.right = area.left + 1;
    if (area.bottom <= area.top) area.bottom = area.top + 1;
    this.chartArea = area;

    /* 3. ticks definitivos, agora com o espaco real */
    /* Reto, o vao e a largura do rotulo. Girado, o que limita e a espessura do
     * texto na perpendicular, nao a largura — cerca de 21 px. E por isso que
     * girar preserva a grade que afinar destruia. */
    var maxX = Math.max(2, Math.floor((area.right - area.left) /
        (this._xRot ? (TICK_PX + 3) / SIN45 : xPitch)) + 1);
    xs.buildTicks(maxX, xLabelFn);
    xs.setRange(area.left, area.right);
    for (i = 0; i < ys.length; i++) {
        ys[i].buildTicks(Math.max(2, Math.floor((area.bottom - area.top) / Y_PITCH)), null);
        ys[i].setRange(area.bottom, area.top);
    }

    /* 4. grade */
    this._drawGrid(ys);

    /* 5. plugins locais (a banda) antes das linhas */
    for (i = 0; i < this.plugins.length; i++) {
        if (this.plugins[i] && this.plugins[i].beforeDatasetsDraw) this.plugins[i].beforeDatasetsDraw(this);
    }

    /* 6. series */
    this._drawSeries(visible);

    /* 7. eixos por cima, para a linha nunca cobrir um rotulo */
    this._drawAxes(ys);
    if (this._legend) this._drawLegend();
    if (this._hover) this._drawTooltip();
};

H5G.prototype._drawGrid = function (ys) {
    var ctx = this.ctx, a = this.chartArea, xs = this.scales.x, i, p;
    var xg = (xs.opt.grid && xs.opt.grid.color) || 'rgba(128,128,128,.2)';
    ctx.save();
    ctx.lineWidth = 1;
    ctx.strokeStyle = xg;
    ctx.beginPath();
    for (i = 0; i < xs.ticks.length; i++) {
        p = Math.round(xs.getPixelForValue(xs.ticks[i])) + 0.5;   /* linha nitida */
        ctx.moveTo(p, a.top); ctx.lineTo(p, a.bottom);
    }
    ctx.stroke();

    /* So o eixo principal desenha grade horizontal: y1 e y2 usam
     * drawOnChartArea:false porque tres grades sobrepostas viram hachura. */
    for (var k = 0; k < ys.length; k++) {
        var s = ys[k];
        if (s.opt.grid && s.opt.grid.drawOnChartArea === false) continue;
        ctx.strokeStyle = (s.opt.grid && s.opt.grid.color) || xg;
        ctx.beginPath();
        for (i = 0; i < s.ticks.length; i++) {
            p = Math.round(s.getPixelForValue(s.ticks[i])) + 0.5;
            ctx.moveTo(a.left, p); ctx.lineTo(a.right, p);
        }
        ctx.stroke();
    }
    ctx.restore();
};

H5G.prototype._drawSeries = function (visible) {
    var ctx = this.ctx, a = this.chartArea, xs = this.scales.x;
    ctx.save();
    ctx.beginPath();
    ctx.rect(a.left, a.top, a.right - a.left, a.bottom - a.top);
    ctx.clip();

    for (var i = 0; i < this.data.datasets.length; i++) {
        if (!visible[i]) continue;
        var ds = this.data.datasets[i];
        var ysc = this.scales[ds.yAxisID || 'y'];
        if (!ysc) continue;
        var d = ds.data || [];

        ctx.lineWidth = ds.borderWidth === undefined ? 1 : ds.borderWidth;
        ctx.strokeStyle = ds.borderColor || '#888';
        ctx.setLineDash(ds.borderDash || []);
        ctx.lineJoin = 'round';
        ctx.lineCap = 'butt';

        /* spanGaps false: um null encerra o traco. Ligar por cima da lacuna
         * inventaria medicao que nunca existiu. */
        var open = false;
        ctx.beginPath();
        for (var j = 0; j < d.length; j++) {
            var p = d[j];
            var y = p ? p.y : null;
            if (y === null || y === undefined) { open = false; continue; }
            var px = xs.getPixelForValue(p.x), py = ysc.getPixelForValue(y);
            if (!open) { ctx.moveTo(px, py); open = true; } else ctx.lineTo(px, py);
        }
        ctx.stroke();
        ctx.setLineDash([]);

        /* Pontos: raio 0 na maioria, e o ponto isolado ganha corpo. Sem isso
         * uma amostra unica entre duas lacunas nao desenha nada e parece dado
         * perdido. */
        var pr = ds.pointRadius;
        if (pr) {
            ctx.fillStyle = ds.borderColor || '#888';
            for (j = 0; j < d.length; j++) {
                var q = d[j];
                if (!q || q.y === null || q.y === undefined) continue;
                var r = typeof pr === 'function' ? pr({ dataset: ds, dataIndex: j, datasetIndex: i }) : pr;
                if (!(r > 0)) continue;
                ctx.beginPath();
                ctx.arc(xs.getPixelForValue(q.x), ysc.getPixelForValue(q.y), r, 0, 6.283185307179586);
                ctx.fill();
            }
        }
    }
    ctx.restore();
};

H5G.prototype._drawAxes = function (ys) {
    var ctx = this.ctx, a = this.chartArea, xs = this.scales.x, i, s, p;
    ctx.save();
    ctx.font = TICK_PX + 'px ' + FONT;
    ctx.textBaseline = 'top';
    ctx.textAlign = 'center';
    ctx.fillStyle = (xs.opt.ticks && xs.opt.ticks.color) || '#888';
    if (this._xRot) {
        /* Gira o contexto e ancora o FIM do texto no tick: assim o rotulo
         * aponta para a sua propria marca, e nao para a vizinha. */
        var rad = -this._xRot * Math.PI / 180;
        ctx.textAlign = 'right';
        ctx.textBaseline = 'middle';
        for (i = 0; i < xs.ticks.length; i++) {
            p = xs.getPixelForValue(xs.ticks[i]);
            ctx.save();
            ctx.translate(p, a.bottom + PAD + TICK_PX * SIN45);
            ctx.rotate(rad);
            ctx.fillText(xs.labels[i], 0, 0);
            ctx.restore();
        }
    } else {
        for (i = 0; i < xs.ticks.length; i++) {
            p = xs.getPixelForValue(xs.ticks[i]);
            ctx.fillText(xs.labels[i], p, a.bottom + PAD);
        }
    }

    for (var k = 0; k < ys.length; k++) {
        s = ys[k];
        var isLeft = (s.opt.position || 'left') === 'left';
        ctx.fillStyle = (s.opt.ticks && s.opt.ticks.color) || '#888';
        ctx.textBaseline = 'middle';
        ctx.textAlign = isLeft ? 'right' : 'left';
        for (i = 0; i < s.ticks.length; i++) {
            p = s.getPixelForValue(s.ticks[i]);
            ctx.fillText(s.labels[i], s._edge, p);
        }
        var t = s.opt.title;
        if (t && t.display) {
            ctx.save();
            ctx.font = 'bold ' + TITLE_PX + 'px ' + FONT;
            ctx.fillStyle = t.color || ctx.fillStyle;
            ctx.textAlign = 'center';
            ctx.textBaseline = isLeft ? 'top' : 'bottom';
            ctx.translate(s._titleX, (a.top + a.bottom) / 2);
            ctx.rotate(isLeft ? -Math.PI / 2 : Math.PI / 2);
            ctx.fillText(t.text, 0, 0);
            ctx.restore();
        }
    }
    ctx.restore();
};

/* ---- legenda ----
 *
 * Sai da lista de datasets, nao de uma copia: esconder uma serie tem de
 * esconder a banda dela junto, e quem le isso e o plugin, pelo
 * isDatasetVisible(). Guardo as caixas de clique porque o desenho acontece
 * antes de qualquer evento chegar. */
H5G.prototype._layoutLegend = function (visible) {
    var lo = (this.options.plugins && this.options.plugins.legend) || {};
    if (lo.display === false || this.data.datasets.length < 2) return null;

    var ctx = this.ctx, items = [], totalW = 0;
    ctx.save();
    ctx.font = LEG_PX + 'px ' + FONT;
    for (var i = 0; i < this.data.datasets.length; i++) {
        var ds = this.data.datasets[i];
        var tw = ctx.measureText(ds.label || '').width;
        var w = LEG_BOX_W + LEG_GAP + tw;
        items.push({ i: i, w: w, tw: tw, label: ds.label || '', ds: ds, on: visible[i] });
        totalW += w;
    }
    ctx.restore();
    totalW += LEG_PAD * (items.length - 1);
    return { items: items, w: totalW, h: LEG_BOX_H + LEG_PAD,
             color: (lo.labels && lo.labels.color) || '#a1a1aa' };
};

H5G.prototype._drawLegend = function () {
    var L = this._legend, ctx = this.ctx;
    var x = Math.round((this.width - L.w) / 2), y = PAD;
    ctx.save();
    ctx.font = LEG_PX + 'px ' + FONT;
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'left';
    for (var k = 0; k < L.items.length; k++) {
        var it = L.items[k], ds = it.ds;
        it.x = x; it.y = y; it.hitH = LEG_BOX_H;      /* area de clique */

        ctx.globalAlpha = it.on ? 1 : 0.45;           /* serie oculta desbota */
        ctx.strokeStyle = ds.borderColor || '#888';
        ctx.lineWidth = ds.borderWidth === undefined ? 1 : ds.borderWidth;
        ctx.setLineDash(ds.borderDash || []);
        if (ds.backgroundColor && ds.backgroundColor !== 'transparent') {
            ctx.fillStyle = ds.backgroundColor;
            ctx.fillRect(x, y, LEG_BOX_W, LEG_BOX_H);
        }
        ctx.strokeRect(x + 0.5, y + 0.5, LEG_BOX_W - 1, LEG_BOX_H - 1);
        ctx.setLineDash([]);

        ctx.fillStyle = L.color;
        var tx = x + LEG_BOX_W + LEG_GAP;
        ctx.fillText(it.label, tx, y + LEG_BOX_H / 2);
        if (!it.on) {
            /* Risco no rotulo: sem ele, "desbotado" e ambiguo com "cor clara". */
            ctx.beginPath();
            ctx.moveTo(tx, y + LEG_BOX_H / 2);
            ctx.lineTo(tx + it.tw, y + LEG_BOX_H / 2);
            ctx.strokeStyle = L.color;
            ctx.lineWidth = 1;
            ctx.stroke();
        }
        ctx.globalAlpha = 1;
        x += it.w + LEG_PAD;
    }
    ctx.restore();
};

/* ---- tooltip ----
 *
 * mode 'nearest' com axis 'x' e intersect false: a distancia so conta em X, e
 * como as series compartilham as mesmas abscissas todas empatam — por isso a
 * caixa lista TODAS as series naquele instante, e nao a mais proxima do
 * cursor. Sem esse empate o tooltip mostraria uma serie so e o usuario
 * perderia a comparacao, que e o motivo de haver tres eixos. */
H5G.prototype._pick = function (mx) {
    var xs = this.scales.x, best = Infinity, cand = [];
    for (var i = 0; i < this.data.datasets.length; i++) {
        if (!this.isDatasetVisible(i)) continue;
        var ds = this.data.datasets[i], d = ds.data || [];
        var bi = -1, bd = Infinity;
        for (var j = 0; j < d.length; j++) {
            var p = d[j];
            if (!p || p.y === null || p.y === undefined) continue;
            var dist = Math.abs(xs.getPixelForValue(p.x) - mx);
            if (dist < bd) { bd = dist; bi = j; }
        }
        if (bi < 0) continue;
        cand.push({ datasetIndex: i, dataIndex: bi, dist: bd, dataset: ds,
                    parsed: { x: d[bi].x, y: d[bi].y } });
        if (bd < best) best = bd;
    }
    if (best > TIP_MAX_DIST) return [];
    return cand.filter(function (c) { return c.dist <= best + 1; });
};

H5G.prototype._drawTooltip = function () {
    var items = this._pick(this._hover.x);
    if (!items.length) return;

    var cb = ((this.options.plugins && this.options.plugins.tooltip) || {}).callbacks || {};
    var title = cb.title ? String(cb.title(items)) : '';
    var lines = items.map(function (it) { return cb.label ? String(cb.label(it)) : ''; });

    var ctx = this.ctx, a = this.chartArea;
    ctx.save();
    ctx.font = 'bold ' + TIP_PX + 'px ' + FONT;
    var w = title ? ctx.measureText(title).width : 0;
    ctx.font = TIP_PX + 'px ' + FONT;
    var sq = TIP_PX;                       /* quadrado de cor por linha */
    for (var i = 0; i < lines.length; i++) {
        w = Math.max(w, sq + LEG_GAP + ctx.measureText(lines[i]).width);
    }
    var lineH = TIP_PX + 4;
    var boxW = w + TIP_PAD * 2;
    var boxH = TIP_PAD * 2 + (title ? lineH : 0) + lines.length * lineH;

    /* Ancorado nos pontos lidos, nao no topo: a caixa presa no alto obriga o
     * olho a subir e descer para ligar valor a ponto. Vira de lado ao encostar
     * na borda direita, senao sairia do canvas. */
    var px = this.scales.x.getPixelForValue(items[0].parsed.x);
    var ysum = 0;
    for (i = 0; i < items.length; i++) {
        ysum += this.scales[items[i].dataset.yAxisID || 'y'].getPixelForValue(items[i].parsed.y);
    }
    var py = ysum / items.length;
    var bx = px + 12, by = py - boxH / 2;
    if (bx + boxW > this.width - 2) bx = px - 12 - boxW;
    if (bx < 2) bx = 2;
    if (by < 2) by = 2;
    if (by + boxH > this.height - 2) by = Math.max(2, this.height - 2 - boxH);

    /* Linha vertical no instante lido: sem ela a caixa flutua sem dizer a que
     * ponto do eixo se refere. */
    ctx.strokeStyle = 'rgba(128,128,128,0.55)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(Math.round(px) + 0.5, a.top);
    ctx.lineTo(Math.round(px) + 0.5, a.bottom);
    ctx.stroke();

    /* Marca no ponto lido de cada serie: sem ela a linha vertical diz o
     * instante mas nao qual amostra de cada serie foi lida. */
    for (i = 0; i < items.length; i++) {
        var ys = this.scales[items[i].dataset.yAxisID || 'y'];
        ctx.beginPath();
        ctx.arc(px, ys.getPixelForValue(items[i].parsed.y), 3, 0, 6.283185307179586);
        ctx.strokeStyle = items[i].dataset.borderColor || '#888';
        ctx.lineWidth = 1.5;
        ctx.stroke();
    }

    ctx.fillStyle = TIP_BG;
    ctx.beginPath();
    if (ctx.roundRect) ctx.roundRect(bx, by, boxW, boxH, TIP_R);
    else ctx.rect(bx, by, boxW, boxH);
    ctx.fill();

    var ty = by + TIP_PAD;
    ctx.textBaseline = 'top';
    ctx.textAlign = 'left';
    if (title) {
        ctx.font = 'bold ' + TIP_PX + 'px ' + FONT;
        ctx.fillStyle = TIP_FG;
        ctx.fillText(title, bx + TIP_PAD, ty);
        ty += lineH;
    }
    ctx.font = TIP_PX + 'px ' + FONT;
    for (i = 0; i < lines.length; i++) {
        ctx.fillStyle = items[i].dataset.borderColor || '#888';
        ctx.fillRect(bx + TIP_PAD, ty + 1, sq, sq - 2);
        ctx.fillStyle = TIP_FG;
        ctx.fillText(lines[i], bx + TIP_PAD + sq + LEG_GAP, ty);
        ty += lineH;
    }
    ctx.restore();
};

/* ---- eventos ----
 * Registrados no construtor e soltos no destroy(): a pagina troca de grafico a
 * cada consulta, e ouvinte esquecido em canvas destruido vaza por sessao. */
H5G.prototype._bind = function () {
    var self = this, c = this.canvas;
    this._onMove = function (e) {
        if (self._destroyed || !self.chartArea) return;
        var a = self.chartArea, x = e.offsetX, y = e.offsetY;
        var inside = x >= a.left && x <= a.right && y >= a.top && y <= a.bottom;
        var was = self._hover;
        self._hover = inside ? { x: x, y: y } : null;
        /* Redesenha so quando a leitura muda de fato: mover o mouse dentro do
         * mesmo pixel nao pode custar um redesenho. */
        if (!!was !== !!self._hover || (self._hover && was && was.x !== self._hover.x)) self.draw();
    };
    this._onLeave = function () {
        if (self._destroyed || !self._hover) return;
        self._hover = null; self.draw();
    };
    this._onClick = function (e) {
        if (self._destroyed || !self._legend) return;
        var x = e.offsetX, y = e.offsetY, its = self._legend.items;
        for (var k = 0; k < its.length; k++) {
            var it = its[k];
            if (x >= it.x && x <= it.x + it.w && y >= it.y && y <= it.y + it.hitH) {
                self._hidden[it.i] = !self._hidden[it.i];
                self.draw();
                return;
            }
        }
    };
    /* Toque: a pagina e usada principalmente no celular, e ouvir so mouse
     * deixava o tooltip inexistente ali. Traduzo o toque para a MESMA rotina de
     * leitura — nao ha caminho separado que possa divergir. Sem preventDefault:
     * bloquear o toque mataria a rolagem da pagina. */
    this._onTouch = function (e) {
        if (self._destroyed || !e.touches || !e.touches.length) return;
        var r = c.getBoundingClientRect(), t = e.touches[0];
        self._onMove({ offsetX: t.clientX - r.left, offsetY: t.clientY - r.top });
    };
    c.addEventListener('mousemove', this._onMove);
    c.addEventListener('mouseleave', this._onLeave);
    c.addEventListener('click', this._onClick);
    c.addEventListener('touchstart', this._onTouch, { passive: true });
    c.addEventListener('touchmove', this._onTouch, { passive: true });
};

H5G.prototype._unbind = function () {
    var c = this.canvas;
    if (!c) return;
    c.removeEventListener('mousemove', this._onMove);
    c.removeEventListener('mouseleave', this._onLeave);
    c.removeEventListener('click', this._onClick);
    c.removeEventListener('touchstart', this._onTouch);
    c.removeEventListener('touchmove', this._onTouch);
};

global.Chart = H5G;
global.H5G = H5G;

})(window);
    </script>
    <style>

        /* History Styles */
        .hist-layout { display: grid; grid-template-columns: 260px 1fr; gap: 20px; align-items: start; margin-bottom: 25px; }
        /* order:2 no 1o filho (seletor de sensores + calendario) poe o grafico
           primeiro: em coluna unica o calendario empurrava o grafico — o conteudo
           da pagina — para ~470px abaixo, fora da primeira tela. */
        @media(max-width: 900px) { .hist-layout { grid-template-columns: 1fr; } .hist-layout > :first-child { order: 2; } }
        .grp label { display: block; color: var(--sub); margin-bottom: 6px; font-size: 0.8rem; font-weight: 700; text-transform: uppercase; }
        .grp select { width: 100%; padding: 8px 12px; background: var(--bg); border: 1px solid var(--border); color: var(--txt); border-radius: 6px; margin-bottom: 15px; font-size: 0.9rem; outline: none; }
        .grp select:focus { border-color: var(--acc); }
        .cal-header-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
        .cal-header-row button { background: var(--bg); padding: 4px 10px; border-radius: 4px; color: var(--txt); border: 1px solid var(--border); cursor: pointer; font-size: 0.8rem;}
        .cal-grid { display: grid; grid-template-columns: repeat(7, 1fr); gap: 4px; text-align: center; font-size: 0.85rem; }
        .cal-dow { color: var(--sub); font-weight: 700; padding-bottom: 4px; font-size: 0.75rem; text-transform: uppercase;}
        .cal-cell { padding: 6px 0; border-radius: 4px; color: #52525b; cursor: default; border: 1px solid transparent; }
        .cal-cell.has-data { background: rgba(6, 182, 212, 0.1); color: var(--acc); cursor: pointer; border: 1px solid rgba(6, 182, 212, 0.3); font-weight: 700; }
        .cal-cell.selected { background: var(--acc); color: #000; font-weight: 800; border-color: #fff; }
        .stats-inline { display: flex; flex-wrap: wrap; gap: 20px; background: var(--bg); border: 1px solid var(--border); padding: 10px 20px; border-radius: 8px; margin-bottom: 15px; justify-content: center; }
        .stat-badge { font-size: 1.1rem; font-weight: 800; display: flex; align-items: center; gap: 8px; }
        .stat-badge span { color: var(--sub); font-weight: 600; font-size: 0.75rem; }
        .hot { color: #ef4444; } .cold { color: #3b82f6; }
        .chart-box { position: relative; height: 45vh; min-height: 280px; width: 100%; resize: vertical; overflow: hidden; border: 1px solid var(--border); border-radius: 8px; background: var(--bg); }
        .chart-overlay { position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; background: rgba(12,15,19,0.85); z-index: 5; transition: opacity 0.3s; pointer-events: none; }
        .chart-overlay.hidden { opacity: 0; }
        .bottom-controls { display: flex; justify-content: center; gap: 8px; margin-top: 20px; flex-wrap: wrap; align-items: center; }
        /* Padronizado: todos os 5 controles tem mesma largura (72px) e altura (38px),
         * texto centralizado, sem mudancas de tamanho/forma entre estados. */
        .bottom-controls > button, .bottom-controls > .csel { width: 72px; height: 38px; box-sizing: border-box; flex-shrink: 0; }
        /* 4 controles x 72px + 3 gaps = 312px contra ~304px uteis: o CSV caia
           sozinho numa 2a linha por 8px. Largura flexivel resolve e ainda da
           alvos maiores. */
        @media(max-width: 640px) { .bottom-controls > button, .bottom-controls > .csel { width: auto; flex: 1 1 0; min-width: 0; height: 44px; } }
        .bottom-controls > button { background: var(--bg); color: var(--txt); border: 1px solid var(--border); border-radius: 6px; cursor: pointer; font-size: 0.85rem; font-weight: 600; display: inline-flex; align-items: center; justify-content: center; padding: 0; }
        .bottom-controls > button.active { background: var(--acc); color: #000; border-color: var(--acc); }
        .bottom-controls > .csel .csel-btn { width: 100%; height: 100%; text-align: center; padding: 0; font-weight: 600; font-size: 0.85rem; display: inline-flex; align-items: center; justify-content: center; }
        .bottom-controls > .csel .csel-arr { display: none; }
        .log-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; flex-wrap: wrap; gap: 10px; }
        /* #logSearch, nao `.log-header input`: o seletor de elemento pegava tambem os
           checkboxes INF/WRN/ERR e a media query abaixo os esticava a 100% de largura.
           font-size 16px impede o zoom automatico do iOS no foco. */
        #logSearch { padding: 8px 12px; background: var(--bg); color: var(--txt); border: 1px solid var(--border); border-radius: 6px; min-width: 200px; outline: none; font-size: 16px;}
        .log-header input:focus { border-color: var(--acc); }
        .log-header button { padding: 8px 12px; background: var(--bg); color: var(--txt); border: 1px solid var(--border); border-radius: 6px; cursor: pointer; font-weight: 600; }
        /* Celular: filtros INF/WRN/ERR continuam em LINHA (3 labels cabem em 360px);
           so a busca e os botoes viram largura cheia. Antes o `> div` inteiro virava
           coluna e cada checkbox ocupava uma linha. */
        @media(max-width: 600px) { .log-header { flex-direction: column; align-items: stretch; } .log-header h2 { text-align: center; } #logSearch { min-width: unset; width: 100%; box-sizing: border-box; } .log-header button { width: 100%; } }
        .log-box { background: var(--bg); border: 1px solid var(--border); border-radius: 8px; max-height: 400px; overflow-y: auto; }
        .log-table { width: 100%; border-collapse: collapse; font-family: monospace; font-size: 0.85rem; }
        .log-table th, .log-table td { padding: 10px 12px; border-bottom: 1px solid var(--border); text-align: left; }
        .log-table th { color: var(--sub); position: sticky; top: 0; background: var(--card); }
        .log-err { color: var(--dang); font-weight: bold;} .log-wrn { color: var(--warn); font-weight: bold;} .log-inf { color: var(--acc); }
        .progress-wrapper { margin: 15px 0; padding: 0 5px; }
        .progress-label { display:flex; justify-content:space-between; margin-bottom:6px; font-size:0.8rem; color:var(--sub); font-weight:700; text-transform:uppercase;}
        .progress-track { width:100%; height:8px; background:var(--bg); border:1px solid var(--border); border-radius:4px; overflow:hidden; }
        .progress-fill { height:100%; width:0%; background: linear-gradient(90deg, var(--acc), #22d3ee); transition: width 0.3s; }
        @keyframes pulse-bg { 0% { opacity: 1; } 50% { opacity: 0.6; } 100% { opacity: 1; } }
        .pulse { animation: pulse-bg 1.5s infinite; }
        /* F-GRAPH.3: progress overlay do export chunked */
        .exp-overlay { position: fixed; inset: 0; background: rgba(12,15,19,0.85); z-index: 9000; display: flex; align-items: center; justify-content: center; }
        .exp-overlay-box { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 24px 32px; min-width: 320px; max-width: 90vw; box-shadow: 0 8px 24px rgba(0,0,0,0.6); }
        .exp-overlay-title { color: var(--txt); font-size: 1.05rem; font-weight: 700; margin-bottom: 14px; }
        .exp-overlay-bar { width: 100%; height: 12px; background: var(--bg); border: 1px solid var(--border); border-radius: 6px; overflow: hidden; margin-bottom: 8px; }
        .exp-overlay-fill { height: 100%; background: linear-gradient(90deg, var(--acc), #22d3ee); width: 0%; transition: width 0.25s; }
        .exp-overlay-stat { color: var(--sub); font-size: 0.82rem; font-weight: 600; display: flex; justify-content: space-between; }
        .exp-overlay-stat .ok { color: var(--ok); }
        .exp-overlay-stat .fail { color: var(--dang); }
        /* F-GRAPH-REVAMP: multi-select dropdown */
        .msel { position: relative; }
        .msel-btn { width: 100%; padding: 8px 12px; background: var(--bg); color: var(--txt); border: 1px solid var(--border); border-radius: 6px; text-align: left; cursor: pointer; font-size: 0.9rem; outline: none; }
        .msel-btn:hover { border-color: var(--acc); }
        .msel-menu { position: absolute; top: calc(100% + 4px); left: 0; right: 0; background: var(--card); border: 1px solid var(--border); border-radius: 6px; max-height: 280px; overflow-y: auto; z-index: 100; padding: 6px; box-shadow: 0 4px 12px rgba(0,0,0,0.4); }
        .msel-menu label { display: flex; align-items: center; gap: 8px; padding: 6px 8px; cursor: pointer; color: var(--txt); font-size: 0.85rem; text-transform: none; font-weight: 500; margin: 0; border-radius: 4px; }
        .msel-menu label:hover { background: var(--bg); }
        .msel-menu input { width: 16px; height: 16px; cursor: pointer; accent-color: var(--acc); margin: 0; }
        .msel-dot { width: 10px; height: 10px; border-radius: 50%; flex-shrink: 0; }
    </style>
    <script>
        /* window.t/applyLang/setLang/showToast/fetchSafe vem de /lang.js */
        document.addEventListener('DOMContentLoaded', () => { setTimeout(applyLang, 50); });
    </script>
</head>
<body>
    <script>installTopbar('nav_hist', 'History &amp; Logs')</script>

    <div class="container">
        <h2 class="page-title" data-i18n="hist_title">Sensor Telemetry</h2>
        <div class="hist-layout">
            <div class="card" style="padding: 16px;">
                <div class="grp">
                    <label data-i18n="hist_src">Sensors</label>
                    <div class="msel" id="sensorMsel">
                        <button type="button" class="msel-btn" id="sensorMselBtn" onclick="_toggleSensorMenu(event)"><span id="sensorMselLbl" data-i18n="hist_loading">Loading...</span><span style="float:right;">▼</span></button>
                        <div class="msel-menu" id="sensorMselMenu" style="display:none;"></div>
                    </div>
                </div>
                <div class="grp" style="margin-bottom:0;">
                    <label data-i18n="hist_cal">Monthly Calendar</label>
                    <div class="cal-header-row">
                        <button onclick="changeMonth(-1)">&#9664;</button>
                        <span id="calMonthYear" style="font-weight:bold; color:var(--txt); font-size: 0.9rem;"></span>
                        <button onclick="changeMonth(1)">&#9654;</button>
                    </div>
                    <div class="cal-grid">
                        <div class="cal-dow" data-i18n="cal_su">Su</div><div class="cal-dow" data-i18n="cal_mo">Mo</div><div class="cal-dow" data-i18n="cal_tu">Tu</div>
                        <div class="cal-dow" data-i18n="cal_we">We</div><div class="cal-dow" data-i18n="cal_th">Th</div><div class="cal-dow" data-i18n="cal_fr">Fr</div><div class="cal-dow" data-i18n="cal_sa">Sa</div>
                    </div>
                    <div class="cal-grid" id="calGrid"></div>
                </div>
            </div>

            <div class="card" style="padding: 16px; display: flex; flex-direction: column; min-width: 0; overflow: hidden;">
                <div class="stats-inline" id="statsGrid" style="opacity:0.3;">
                    <!-- Filled by renderExtremes() from the server's extremes{}:
                         two badges per channel that produced a reading. Was six
                         hardcoded badges, which is why pressure had nowhere to
                         land even after the server started measuring it. -->
                </div>

                <div id="chartContainer">
                    <div style="font-weight:bold; color:var(--acc); margin-bottom:10px;" id="chartTitle">--</div>
                    <div class="chart-box" id="chartBox">
                        <div id="chartOverlay" class="chart-overlay">
                            <span id="overlayMsg" style="color:var(--sub);" data-i18n="hist_loading">Loading...</span>
                            <div class="progress-wrapper" id="progressWrap" style="display:none; width:80%; margin-top:10px;">
                                <div class="progress-label">
                                    <span id="progStatus" data-i18n="hist_loading">Loading...</span>
                                    <span class="prog-detail" id="progDetail" style="color:var(--acc); font-family:monospace;">0 KB</span>
                                </div>
                                <div class="progress-track"><div class="progress-fill pulse" id="progFill"></div></div>
                            </div>
                        </div>
                        <canvas id="myChart"></canvas>
                    </div>
                    <div class="bottom-controls">
                        <button onclick="navGraph(-1)" id="btnPrev" title="Anterior">◀</button>
                        <select id="rangeSel" title="Intervalo" onchange="loadGraphRange(parseInt(this.value,10))">
                            <option value="0">1h</option>
                            <option value="1">6h</option>
                            <option value="2" selected>24h</option>
                            <option value="3">7d</option>
                            <option value="4">1M</option>
                            <option value="5">1A</option>
                            <option value="6">MAX</option>
                        </select>
                        <button onclick="navGraph(+1)" id="btnNext" title="Próximo">▶</button>
                        <button onclick="exportHistoryCsv()" id="btnExpHist" title="Export CSV">⤓ CSV</button>
                    </div>
                </div>
            </div>
        </div>

        <div class="card" style="margin-top: 20px;">
            <div class="log-header">
                <h2 class="page-title" data-i18n="hist_logs" style="margin:0;">System Event Logs</h2>
                <div style="display:flex; gap:10px; align-items:center; flex-wrap:wrap;">
                    <label style="display:flex;align-items:center;gap:4px;font-size:0.8rem;color:var(--acc);cursor:pointer;"><input type="checkbox" id="chkInf" onchange="filterLogs()"> INF</label>
                    <label style="display:flex;align-items:center;gap:4px;font-size:0.8rem;color:var(--warn);cursor:pointer;"><input type="checkbox" id="chkWrn" onchange="filterLogs()"> WRN</label>
                    <label style="display:flex;align-items:center;gap:4px;font-size:0.8rem;color:var(--dang);cursor:pointer;"><input type="checkbox" id="chkErr" onchange="filterLogs()" checked> ERR</label>
                    <input type="text" id="logSearch" placeholder="Filter events..." data-i18n="hist_filt" onkeyup="filterLogs()">
                    <button id="btnLoadLogs" onclick="loadLogs()" data-i18n="hist_load_btn" style="color:var(--acc); border-color:var(--acc);">Load</button>
                    <button id="btnExpLogs" onclick="exportLogsCsv()" title="Export CSV" data-i18n="exp_btn">⤓ CSV</button>
                    <button onclick="clearLogs()" style="border-color:var(--dang); color:var(--dang); background:transparent;" data-i18n="hist_clear">Clear</button>
                </div>
            </div>

            <div class="progress-wrapper" id="logProgressWrap" style="display:none; margin-top: 0; margin-bottom: 15px; padding: 0;">
                <div class="progress-label">
                    <span id="logProgStatus" data-i18n="hist_loading">Loading...</span>
                    <span class="prog-detail" id="logProgDetail" style="color:var(--acc); font-family:monospace;">0 KB</span>
                </div>
                <div class="progress-track"><div class="progress-fill" id="logProgFill"></div></div>
            </div>

            <div class="log-box">
                <table class="log-table">
                    <thead><tr><th style="width:155px" data-i18n="hist_dtlog">Date & Time</th><th style="width:95px" data-i18n="hist_uptime">Uptime</th><th style="width:50px" data-i18n="hist_lvl">Level</th><th style="width:70px" data-i18n="hist_module">Module</th><th data-i18n="hist_desc">Event Description</th></tr></thead>
                    <tbody id="logBody"><tr><td colspan="5" style="padding:20px; text-align:center; color:var(--sub);" data-i18n="hist_prompt">Click 'Load' to view system logs.</td></tr></tbody>
                </table>
            </div>
        </div>
    </div>

    <script>
        let myChart = null; let availDates = []; let currentCalDate = new Date(); let logsLoadedOnce = false;
        let anchorEnd = 0; let currentRange = -1;
        let _lastChartCutoff = 0; let _lastChartEnd = 0; /* range visualizado — usado por exportHistoryCsv */
        /* F-GRAPH-REVAMP: 7 niveis 1h, 6h, 24h, 7d, 1M, 1A, MAX (idx 6 = 0 = ilimitado) */
        const rangeDurations = [3600, 21600, 86400, 604800, 2592000, 31536000, 0];
        let _selectedSensors = []; /* preenchido com todos os slots ativos no populate */
        let _currentRangeIdx = 2;    /* default: 24h */

        /* Paleta para series. Cores quentes p/ T (variando por idx do sensor),
         * azul fixo p/ umidade. */
        const _hotColors = ['#e5484d', '#f76b15', '#d97706', '#e93d82', '#c2410c', '#9f6b00'];
        const _humColor  = '#3b82f6';
        const _pressColor = '#8250df';
        /* Grade e ticks acompanham o tema: mesmos tokens das bordas/texto-sub.
         * Lidos a cada render — trocar o tema vale a partir do proximo grafico. */
        function _themeVar(name, fb) { const v = getComputedStyle(document.documentElement).getPropertyValue(name); return (v || fb).trim(); }
        function _gridColor() { return _themeVar('--border', '#2a3340'); }
        function _tickColor() { return _themeVar('--sub', '#98a6b3'); }
        function _seriesColor(serieIdx, isHum) { return isHum ? _humColor : _hotColors[serieIdx % _hotColors.length]; }

        async function loadAvailableDays() { try { const res = await fetchSafe('/api/history_days'); availDates = await res.json(); renderCalendar(); } catch(e) {} }

        async function populateSensorMsel() {
            try {
                const res = await fetchSafe('/api/status');
                if (!res.ok) return;
                const data = await res.json();
                const menu = document.getElementById('sensorMselMenu');
                if (!menu) return;
                menu.innerHTML = '';
                /* /api/status emite {slot, id, name, ...}. O codigo antigo
                 * especializava o slot 10 legado ("Ambiente Central") e o
                 * default [10]: sem slot 10 na config atual, nada nascia
                 * selecionado => grafico em branco. */
                const items = [];
                if (data && data.sensors) {
                    data.sensors.forEach(s => items.push({ id: s.slot, name: s.name, hwId: s.id }));
                }
                /* Default: apenas o primeiro sensor da lista.
                 * Abrir com todos marcados empilha uma serie de T (mais uma de
                 * H nos que tem) por sensor no mesmo grafico, o que polui a
                 * leitura inicial e faz cada troca de intervalo varrer o
                 * historico de todos eles. Quem quiser comparar marca os
                 * outros no seletor; a escolha persiste entre recargas. */
                if (_selectedSensors.length === 0 ||
                    !items.some(it => _selectedSensors.indexOf(it.id) >= 0)) {
                    _selectedSensors = items.length ? [items[0].id] : [];
                }
                items.forEach(it => {
                    const lbl = document.createElement('label');
                    const chk = document.createElement('input');
                    chk.type = 'checkbox';
                    chk.value = String(it.id);
                    chk.checked = (_selectedSensors.indexOf(it.id) >= 0);
                    chk.onchange = _onSensorMselChange;
                    const dot = document.createElement('span');
                    dot.className = 'msel-dot';
                    /* idx no items vira a cor da série quando montar o chart */
                    const colorIdx = items.indexOf(it);
                    dot.style.background = _seriesColor(colorIdx, false);
                    const txt = document.createElement('span');
                    txt.textContent = `${it.hwId} — ${it.name}`;
                    lbl.appendChild(chk); lbl.appendChild(dot); lbl.appendChild(txt);
                    menu.appendChild(lbl);
                });
                _updateSensorMselLabel();
            } catch(e) {}
        }

        function _onSensorMselChange() {
            const checks = document.querySelectorAll('#sensorMselMenu input[type=checkbox]');
            const sel = [];
            checks.forEach(c => { if (c.checked) sel.push(parseInt(c.value, 10)); });
            if (sel.length === 0 && checks.length > 0) {
                /* impede deselecao total — re-marca o primeiro slot real */
                checks[0].checked = true;
                sel.push(parseInt(checks[0].value, 10));
            }
            _selectedSensors = sel;
            _updateSensorMselLabel();
            /* Recarrega grafico */
            loadGraphRange(_currentRangeIdx);
        }

        function _updateSensorMselLabel() {
            const lbl = document.getElementById('sensorMselLbl');
            if (!lbl) return;
            const n = _selectedSensors.length;
            if (n === 0) { lbl.textContent = window.t('hist_none_sel', 'None'); return; }
            if (n === 1) {
                const checks = document.querySelectorAll('#sensorMselMenu input[type=checkbox]:checked');
                if (checks.length > 0) {
                    const lblTxt = checks[0].parentNode.querySelector('span:last-child');
                    if (lblTxt) { lbl.textContent = lblTxt.textContent; return; }
                }
            }
            lbl.textContent = `${n} ${window.t('hist_n_sel', 'selected')}`;
        }

        function _toggleSensorMenu(ev) {
            if (ev) ev.stopPropagation();
            const menu = document.getElementById('sensorMselMenu');
            if (!menu) return;
            menu.style.display = (menu.style.display === 'block') ? 'none' : 'block';
        }
        document.addEventListener('click', e => {
            const m = document.getElementById('sensorMselMenu');
            const b = document.getElementById('sensorMselBtn');
            if (!m || !b) return;
            if (m.style.display === 'block' && !m.contains(e.target) && !b.contains(e.target)) m.style.display = 'none';
        });
        function changeMonth(dir) { currentCalDate.setMonth(currentCalDate.getMonth() + dir); renderCalendar(); }
        function renderCalendar() { const y = currentCalDate.getFullYear(); const m = currentCalDate.getMonth(); const months = [window.t('m_jan','Jan'), window.t('m_feb','Feb'), window.t('m_mar','Mar'), window.t('m_apr','Apr'), window.t('m_may','May'), window.t('m_jun','Jun'), window.t('m_jul','Jul'), window.t('m_aug','Aug'), window.t('m_sep','Sep'), window.t('m_oct','Oct'), window.t('m_nov','Nov'), window.t('m_dec','Dec')]; document.getElementById('calMonthYear').innerText = months[m] + " " + y; const grid = document.getElementById('calGrid'); grid.innerHTML = ''; const firstDay = new Date(y, m, 1).getDay(); const daysInMonth = new Date(y, m + 1, 0).getDate(); for(let i=0; i<firstDay; i++) grid.innerHTML += `<div class="cal-cell"></div>`; for(let d=1; d<=daysInMonth; d++) { let ds = (d<10?'0':'') + d; let ms = ((m+1)<10?'0':'') + (m+1); let dateStr = `${y}${ms}${ds}`; let isAvail = availDates.includes(dateStr); let cls = isAvail ? 'cal-cell has-data' : 'cal-cell'; let onclick = isAvail ? `onclick="loadGraphDate('${dateStr}', this)"` : ''; grid.innerHTML += `<div class="${cls}" id="cal_${dateStr}" ${onclick}>${d}</div>`; } }
        function fmt(v){return v < 10 ? '0' + v : v;}
        async function loadGraphRange(range) {
            document.querySelectorAll('.cal-cell').forEach(c => c.classList.remove('selected'));
            _currentRangeIdx = range;
            currentRange = range;
            /* Sincroniza select (helper global _makeCustomSelect re-renderiza UI) */
            const sel = document.getElementById('rangeSel');
            if (sel && sel.value !== String(range)) sel.value = String(range);
            /* Preserva âncora se existir */
            let q = `range=${range}`;
            if (anchorEnd > 0) q += `&end=${anchorEnd}`;
            await fetchAndDraw(q);
        }
        async function loadGraphDate(dateStr, cellElement) {
            document.querySelectorAll('.cal-cell').forEach(c => c.classList.remove('selected'));
            cellElement.classList.add('selected');
            /* Ancora na meia-noite do dia seguinte → janela mostra dia inteiro */
            let y = parseInt(dateStr.substring(0,4));
            let m = parseInt(dateStr.substring(4,6)) - 1;
            let d = parseInt(dateStr.substring(6,8));
            anchorEnd = Math.floor(new Date(y, m, d + 1).getTime() / 1000);
            await loadGraphRange(2); /* 24h */
        }
        async function navGraph(dir) {
            if (_currentRangeIdx < 0) return;
            const step = rangeDurations[_currentRangeIdx] || 86400;
            const nowEpoch = Math.floor(Date.now() / 1000);
            if (anchorEnd === 0) anchorEnd = nowEpoch;
            anchorEnd += dir * step;
            if (anchorEnd > nowEpoch) anchorEnd = nowEpoch;
            await fetchAndDraw(`range=${_currentRangeIdx}&end=${anchorEnd}`);
        }
        function showOverlay(msg) { const ov = document.getElementById('chartOverlay'); const om = document.getElementById('overlayMsg'); ov.classList.remove('hidden'); om.innerText = msg || ''; }
        function hideOverlay() { document.getElementById('chartOverlay').classList.add('hidden'); }
        function showProgress(show) { const w = document.getElementById('progressWrap'); const f = document.getElementById('progFill'); if (show) { w.style.display='block'; f.style.width='0%'; f.classList.add('pulse'); } else { f.classList.remove('pulse'); setTimeout(()=>{ w.style.display='none'; }, 400); } }
        function finishProgress() { const f = document.getElementById('progFill'); f.classList.remove('pulse'); f.style.width = '100%'; document.getElementById('progStatus').innerText = window.t('hist_done', 'Complete'); }
        /* ── Grafico direto dos arquivos .h5 ────────────────────────────────
         *
         * O caminho antigo pedia ao aparelho um JSON ja reamostrado pelo
         * /api/history_multi: stride de 1 em N registros no caminho decode
         * e, alem de um certo tamanho, o envelope de bloco - dois pontos por
         * hora, o minimo em t0 e o maximo em t0+30min, desenhados como se
         * fossem a serie. Era isso que fazia uma faixa de 1h ancorada no
         * passado chegar com 3 pontos, e o 24h virar uma serra min-max que
         * nao parece com o dado gravado (medido na bancada: 1h=3, 6h=13,
         * 24h=51 pontos, com o degelo da geladeira duplicado pela alternancia
         * min/max de blocos vizinhos).
         *
         * Agora a pagina baixa os proprios arquivos por /download - o mesmo
         * caminho do export CSV - decodifica aqui com h5Decode, e quem reduz
         * para a tela e o navegador, por balde de pixel guardando minimo,
         * maximo e media. Pico de 1 minuto sobrevive porque o extremo E o
         * ponto; lacuna real vira lacuna desenhada; e o numero de pontos
         * acompanha a largura do canvas em qualquer faixa.
         *
         * Cache por arquivo: um dia fechado nunca muda, entao trocar de
         * faixa ou de sensor nao baixa nada de novo. So o dia corrente e a
         * hora aberta (servida em /api/history/open no mesmo formato) sao
         * rebuscados. Download do mais novo para o mais antigo: cancelar no
         * meio deixa justamente a parte recente na tela.
         */
        const H5G_BUCKET_MIN = 300, H5G_BUCKET_MAX = 1200;
        const H5G_RAW_FACTOR = 1.6;   /* ate buckets*fator registros vai cru */
        const H5G_LS_TTL_MS = 45000, H5G_META_TTL_MS = 60000;
        const H5G_KIND = { 1: { letter: 't', key: 'temp',  unit: '°C',  label: 'ch_temp',  dec: 2 },
                           2: { letter: 'u', key: 'hum',   unit: '%',   label: 'ch_hum',   dec: 1 },
                           3: { letter: 'p', key: 'press', unit: 'hPa', label: 'ch_press', dec: 1 } };
        let _gchAbort = null;
        let _gchCancelled = false;
        let _gchGen = 0;                    /* newest graph load wins */
        let _gchChain = Promise.resolve();  /* serialize loads: one transfer at a time */
        let _h5Meta = null, _h5MetaAt = 0;
        let _h5LsCache = null, _h5LsAt = 0;
        const _h5Raw = new Map();           /* nome -> {size, buf} - reuso do CSV  */
        const _h5Cols = new Map();          /* nome -> {size, recs, t, cols} */

        /* Botao de cancelar dentro da barra de progresso. pointer-events e
         * z-index: o overlay do grafico desliga eventos para nao roubar o
         * pan, e o botao precisa reativa-los para ser clicavel. */
        function _gchCancelUi(on) {
            let b = document.getElementById('gchX');
            if (on && !b) {
                const wrap = document.getElementById('progressWrap');
                if (!wrap) return;
                b = document.createElement('button');
                b.id = 'gchX'; b.type = 'button';
                b.textContent = window.t('hist_cancel', 'Cancel');
                b.style.cssText = 'position:relative;z-index:6;pointer-events:auto;'
                    + 'touch-action:manipulation;margin-top:8px;padding:8px 14px;'
                    + 'background:transparent;color:var(--dang);border:1px solid var(--dang);'
                    + 'border-radius:6px;cursor:pointer;font-weight:600;font-size:.8rem';
                b.onclick = () => {
                    _gchCancelled = true;
                    if (_gchAbort) try { _gchAbort.abort(); } catch (e) {}
                    b.disabled = true; b.textContent = '...';
                };
                wrap.appendChild(b);
            } else if (!on && b) {
                b.remove();
            }
        }

        function _h5ShowProg(done, total, kb) {
            const wrap = document.getElementById('progressWrap');
            const fill = document.getElementById('progFill');
            if (wrap) wrap.style.display = 'block';
            if (fill) {
                fill.classList.remove('pulse');
                fill.style.width = Math.round(done * 100 / Math.max(1, total)) + '%';
            }
            const det = document.getElementById('progDetail');
            const sta = document.getElementById('progStatus');
            if (det) det.innerText = kb.toFixed(1) + ' KB';
            if (sta) sta.innerText = window.t('hist_files', 'Files') + ': ' + done + '/' + total;
        }

        async function _h5GetMeta(force) {
            const nowMs = Date.now();
            if (!force && _h5Meta && nowMs - _h5MetaAt < H5G_META_TTL_MS) return _h5Meta;
            const r = await fetchSafe('/api/status', { timeout: 10000, retries: 1 });
            const j = await r.json();
            const slots = {};
            (j.sensors || []).forEach(s => { slots[s.slot] = { hwId: s.id, name: s.name }; });
            /* skew: o relogio do aparelho manda na janela, nao o do PC */
            _h5Meta = { nominal: (j.sys && j.sys.hi > 0) ? j.sys.hi * 60 : 60,
                        slots,
                        skew: (j.sys && j.sys.time) ? (j.sys.time - nowMs / 1000) : 0 };
            _h5MetaAt = nowMs;
            return _h5Meta;
        }
        function _h5Now() { return Math.floor(Date.now() / 1000 + (_h5Meta ? _h5Meta.skew : 0)); }
        function _h5DayStart(ep) {
            const d = new Date(ep * 1000);
            return Math.floor(new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime() / 1000);
        }

        async function _h5GetLs(force) {
            const nowMs = Date.now();
            if (!force && _h5LsCache && nowMs - _h5LsAt < H5G_LS_TTL_MS) return _h5LsCache;
            const r = await fetchSafe('/api/ls?dir=/history', { timeout: 12000, retries: 2 });
            const j = await r.json();
            const out = [];
            for (const e of (j.entries || [])) {
                const m = /^(\d{4})(\d{2})(\d{2})\.h5$/.exec(e.n);
                if (!m) continue;
                /* dia LOCAL do navegador == dia local do aparelho (mesmo fuso,
                 * mesma premissa que o export CSV ja faz) */
                const t0 = Math.floor(new Date(+m[1], +m[2] - 1, +m[3]).getTime() / 1000);
                out.push({ name: e.n, size: e.s || 0, t0 });
            }
            out.sort((a, b) => a.t0 - b.t0);
            _h5LsCache = out; _h5LsAt = nowMs;
            return out;
        }

        /* Colunar por arquivo: um Float64Array de epochs e um Int16Array por
         * canal, com sentinela onde o canal nao existia ainda (arquivo antigo
         * com schema menor). O dispositivo inteiro decodificado cabe folgado
         * na memoria da pagina. */
        function _h5Columnar(buf, nominal) {
            const dec = h5Decode((buf instanceof Uint8Array) ? buf : new Uint8Array(buf), nominal);
            const n = dec.series.length;
            const t = new Float64Array(n);
            const byId = new Map();
            for (let i = 0; i < n; i++) {
                const rec = dec.series[i];
                t[i] = rec.t;
                for (let c = 0; c < rec.schema.length; c++) {
                    const d = rec.schema[c];
                    let col = byId.get(d.id);
                    if (!col) {
                        col = { id: d.id, kind: d.kind, scale: Math.pow(10, d.scaleExp),
                                v: new Int16Array(n).fill(H5_NAN) };
                        byId.set(d.id, col);
                    }
                    col.v[i] = rec.v[c];
                }
            }
            return { recs: n, rejected: dec.rejected, t, cols: Array.from(byId.values()) };
        }

        /* Reamostragem por balde de pixel. Abaixo do limiar desenha cru (com
         * null no meio de lacunas reais); acima, cada balde guarda minimo,
         * maximo e media, e balde vazio vira null. O registro mais novo sai
         * sempre com o proprio carimbo - a borda em dia e o objetivo. */
        function _h5Decimate(ts, vs, fromEp, toEp, nominal, buckets) {
            const n = ts.length;
            if (n === 0) return { mode: 'raw', pts: [] };
            if (n <= buckets * H5G_RAW_FACTOR) {
                const gapThr = Math.max(3.5 * nominal, 90);
                const pts = [];
                for (let i = 0; i < n; i++) {
                    if (i > 0 && ts[i] - ts[i - 1] > gapThr)
                        pts.push({ x: (ts[i - 1] + (ts[i] - ts[i - 1]) / 2) * 1000, y: null });
                    pts.push({ x: ts[i] * 1000, y: vs[i] });
                }
                return { mode: 'raw', pts };
            }
            const w = (toEp - fromEp) / buckets;
            const mn = new Float64Array(buckets).fill(Infinity);
            const mx = new Float64Array(buckets).fill(-Infinity);
            const sum = new Float64Array(buckets);
            const cnt = new Uint32Array(buckets);
            const tsum = new Float64Array(buckets);
            for (let i = 0; i < n; i++) {
                let b = Math.floor((ts[i] - fromEp) / w);
                if (b < 0) b = 0;
                if (b >= buckets) b = buckets - 1;
                const v = vs[i];
                if (v < mn[b]) mn[b] = v;
                if (v > mx[b]) mx[b] = v;
                sum[b] += v; cnt[b]++; tsum[b] += ts[i];
            }
            const lo = [], hi = [], mid = [];
            let inGap = false;
            for (let b = 0; b < buckets; b++) {
                if (cnt[b] === 0) {
                    if (!inGap && lo.length) {
                        const x = (fromEp + (b + 0.5) * w) * 1000;
                        lo.push({ x, y: null }); hi.push({ x, y: null }); mid.push({ x, y: null });
                        inGap = true;
                    }
                    continue;
                }
                inGap = false;
                const x = (tsum[b] / cnt[b]) * 1000;
                lo.push({ x, y: mn[b] });
                hi.push({ x, y: mx[b] });
                mid.push({ x, y: sum[b] / cnt[b] });
            }
            const lastX = ts[n - 1] * 1000;
            if (mid.length && mid[mid.length - 1].x < lastX) {
                lo.push({ x: lastX, y: vs[n - 1] });
                hi.push({ x: lastX, y: vs[n - 1] });
                mid.push({ x: lastX, y: vs[n - 1] });
            }
            return { mode: 'band', lo, hi, mid };
        }

        async function _h5Fetch(path, signal) {
            const r = await fetch(path, { credentials: 'include', signal });
            if (!r.ok) throw new Error('HTTP ' + r.status);
            return await r.arrayBuffer();
        }

        /* Busca e monta a janela inteira a partir dos arquivos. Folga de um
         * dia na escolha: um bloco selado pode cruzar a meia-noite e carregar
         * registros do dia seguinte no arquivo do dia do t0. */
        async function _h5Load(fromEp, toEp, sensorSel, myGen) {
            const meta = await _h5GetMeta(false);
            const ls = await _h5GetLs(false);
            const wantAll = (fromEp <= 0);
            const nowEp = _h5Now();
            const todayT0 = _h5DayStart(nowEp);
            const files = ls.filter(f => wantAll || (f.t0 <= toEp && f.t0 + 2 * 86400 > fromEp));
            if (wantAll && files.length) fromEp = files[0].t0;

            const fetchList = files.slice().sort((a, b) => b.t0 - a.t0);
            const holes = [];
            let done = 0, kb = 0;
            _h5ShowProg(0, fetchList.length, 0);
            for (const f of fetchList) {
                if (_gchCancelled || myGen !== _gchGen) break;
                const cached = _h5Cols.get(f.name);
                if (cached && cached.size === f.size && f.t0 !== todayT0) {
                    done++; _h5ShowProg(done, fetchList.length, kb);
                    continue;
                }
                let got = null;
                for (let att = 0; att < 3 && !got && !_gchCancelled; att++) {
                    try {
                        _gchAbort = new AbortController();
                        const buf = await _h5Fetch('/download?file=/history/' + f.name, _gchAbort.signal);
                        kb += buf.byteLength / 1024;
                        got = _h5Columnar(buf, meta.nominal);
                        got.size = buf.byteLength;
                        _h5Raw.set(f.name, { size: buf.byteLength, buf });
                        _h5Cols.set(f.name, got);
                    } catch (e) {
                        if (e.name === 'AbortError') break;
                        if (att < 2) await new Promise(s => setTimeout(s, 300 * (att + 1)));
                    }
                }
                _gchAbort = null;
                if (!got && !_gchCancelled) holes.push(f.name);
                done++;
                _h5ShowProg(done, fetchList.length, kb);
            }

            /* Montagem das series, em ordem de arquivo. Duplicatas (uma selagem
             * no meio da carga poe o mesmo registro no arquivo E na cauda) saem
             * na normalizacao adiante, junto com a ordenacao por instante. */
            const sel = new Set(sensorSel.map(Number));
            const series = new Map();
            const extremes = {};
            let tsMinT = 0, tsMaxT = 0, recsInWin = 0;
            const pushCols = (cs) => {
                const tArr = cs.t, m = cs.recs;
                for (const col of cs.cols) {
                    const slot = col.id >> 3;
                    if (!sel.has(slot)) continue;
                    const ki = H5G_KIND[col.kind];
                    if (!ki) continue;
                    const sm = meta.slots[slot];
                    const hwId = sm ? sm.hwId : ('S' + slot);
                    const key = ki.letter + hwId;
                    let s = series.get(key);
                    if (!s) {
                        s = { key, slot, kind: col.kind, hwId, ts: [], vs: [] };
                        series.set(key, s);
                    }
                    const v = col.v, sc = col.scale;
                    let q = extremes[ki.key];
                    if (!q) q = extremes[ki.key] = { min: Infinity, max: -Infinity, unit: ki.unit, label: ki.label };
                    for (let i = 0; i < m; i++) {
                        const t = tArr[i];
                        if (t < fromEp || t > toEp || v[i] === H5_NAN) continue;
                        const rv = v[i] * sc;
                        s.ts.push(t); s.vs.push(rv);
                        recsInWin++;
                        if (rv < q.min) { q.min = rv; if (col.kind === 1) tsMinT = t; }
                        if (rv > q.max) { q.max = rv; if (col.kind === 1) tsMaxT = t; }
                    }
                }
            };
            for (const f of files) {
                const cs = _h5Cols.get(f.name);
                if (cs) pushCols(cs);
            }

            /* A cauda por ULTIMO (regra da hora aberta): se uma selagem cair
             * no meio, o pior caso e lacuna, nunca duplicata. */
            let ramRecs = 0;
            if (!_gchCancelled && myGen === _gchGen && toEp >= nowEp - 4500) {
                try {
                    const r = await fetchSafe('/api/history/open', { timeout: 10000, retries: 1 });
                    if (r.ok && r.status !== 204) {
                        const buf = await r.arrayBuffer();
                        if (buf.byteLength > 0) {
                            const cs = _h5Columnar(buf, meta.nominal);
                            ramRecs = cs.recs;
                            pushCols(cs);
                        }
                    }
                } catch (e) { /* a cauda e bonus, nao requisito */ }
            }

            /* Normalizacao: ordena por instante e descarta duplicatas. O guarda
             * antigo era "t <= last" no laco acima, que trata fora-de-ordem
             * como duplicata e descarta TUDO que vier depois de um bloco
             * carimbado a frente. Na noite de 14/08 isso apagou do grafico tres
             * blocos validos que estavam no flash o tempo todo, entre eles os
             * 31 registros de 23:29 as 23:59 com carimbo correto. Deduplicar
             * exige comparar com o conjunto, nao com o vizinho; ordenar depois
             * mantem a deteccao de lacuna e o "ultimo valor" corretos. */
            recsInWin = 0;
            for (const s of series.values()) {
                const n = s.ts.length;
                if (n > 1) {
                    const idx = new Array(n);
                    for (let i = 0; i < n; i++) idx[i] = i;
                    idx.sort((a, b) => s.ts[a] - s.ts[b]);
                    const ts = new Array(n), vs = new Array(n);
                    let m = 0;
                    for (let i = 0; i < n; i++) {
                        const t = s.ts[idx[i]];
                        if (m && ts[m - 1] === t) continue;
                        ts[m] = t; vs[m] = s.vs[idx[i]]; m++;
                    }
                    ts.length = m; vs.length = m;
                    s.ts = ts; s.vs = vs;
                }
                /* Recontado aqui: o laco de montagem soma antes de deduplicar,
                 * e a cauda repete o que a selagem ja gravou. */
                recsInWin += s.ts.length;
            }

            for (const k in extremes) {
                if (!isFinite(extremes[k].min)) delete extremes[k];
            }

            const cv = document.getElementById('myChart');
            const wPx = (cv && cv.clientWidth) ? cv.clientWidth : 900;
            const buckets = Math.max(H5G_BUCKET_MIN, Math.min(H5G_BUCKET_MAX, Math.floor(wPx)));
            const out = [];
            for (const s of series.values()) {
                out.push({ key: s.key, slot: s.slot, kind: s.kind, hwId: s.hwId,
                           n: s.ts.length,
                           dec: _h5Decimate(s.ts, s.vs, fromEp, toEp, meta.nominal, buckets) });
            }
            return { series: out, extremes, tsMinT, tsMaxT, cutoff: fromEp, end: toEp,
                     recs: recsInWin, ram: ramRecs, holes,
                     cancelled: _gchCancelled, files: files.length };
        }

        /* Coordinator: serialize graph loads so exactly one transfer is ever live,
         * and let the newest win. Each call cancels the load in flight (abort +
         * flag) and queues behind it on _gchChain; a load superseded before it even
         * starts is skipped by the generation check. Changing the sensor selection,
         * range or date mid-load used to start a second slice loop over the first —
         * two loops racing on one progress bar and on the shared _gchAbort, and
         * firing overlapping requests at the device. That overlap is the "confused
         * loading bar" the user reported, and the same overlap that tripped the
         * drain reboot (D-B8c); one-at-a-time removes both at the source. */
        function fetchAndDraw(queryParam) {
            const myGen = ++_gchGen;
            _gchCancelled = true;
            if (_gchAbort) { try { _gchAbort.abort(); } catch (e) {} }
            _gchChain = _gchChain.then(async () => {
                if (myGen !== _gchGen) return;   /* a newer load already superseded this one */
                _gchCancelled = false;
                await _doFetchAndDraw(queryParam, myGen);
            }).catch(() => {});
            return _gchChain;
        }

        async function _doFetchAndDraw(queryParam, myGen) {
            const gridBox = document.getElementById('statsGrid');
            const rm = queryParam.match(/range=(\d)/);
            const rangeIdx = rm ? parseInt(rm[1], 10) : 2;
            const em = queryParam.match(/end=(\d+)/);
            try {
                gridBox.style.opacity = '0.3';
                showOverlay(window.t('hist_down_msg', 'Downloading...'));
                showProgress(true);
                document.getElementById('progStatus').innerText = window.t('hist_loading', 'Loading...');
                _gchCancelUi(true);
                let res;
                try {
                    await _h5GetMeta(false);
                    const dur = rangeDurations[rangeIdx] || 0;
                    const toEp = em ? Math.min(parseInt(em[1], 10), _h5Now()) : _h5Now();
                    const fromEp = dur > 0 ? toEp - dur : 0;
                    res = await _h5Load(fromEp, toEp, _selectedSensors, myGen);
                } catch (e) {
                    if (_gchGen === myGen) {
                        _gchCancelUi(false); showProgress(false);
                        showOverlay(window.t('hist_err_json', 'Error'));
                    }
                    return;
                }
                if (_gchGen !== myGen) return;
                _gchCancelUi(false);
                finishProgress();
                if (res.holes.length || res.cancelled) {
                    /* Parcial e melhor que nada, mas o usuario precisa saber
                       que e parcial - um grafico com buraco silencioso e pior
                       que um aviso. */
                    showToast(window.t('hist_partial', 'Partial: ') + res.holes.length + ' '
                              + window.t('hist_lost', 'lost'), 'warn');
                }
                const totalPts = res.series.reduce((a, s) => a + s.n, 0);
                if (totalPts === 0) {
                    if (myChart) { myChart.destroy(); myChart = null; }
                    showProgress(false);
                    showOverlay(window.t('hist_no_data', 'No data for this period'));
                    return;
                }
                setTimeout(() => showProgress(false), 400);
                const cutoffEpoch = res.cutoff, endEpoch = res.end;
                _lastChartCutoff = cutoffEpoch; _lastChartEnd = endEpoch;
                /* Titulo */
                if (cutoffEpoch > 0 && endEpoch > 0) {
                    const dC = new Date(cutoffEpoch * 1000), dE = new Date(endEpoch * 1000);
                    const sameDay = (dC.getDate() === dE.getDate() && dC.getMonth() === dE.getMonth());
                    document.getElementById('chartTitle').innerText = sameDay
                        ? `${fmt(dC.getDate())}/${fmt(dC.getMonth()+1)}  ${fmt(dC.getHours())}:${fmt(dC.getMinutes())} - ${fmt(dE.getHours())}:${fmt(dE.getMinutes())}`
                        : `${fmt(dC.getDate())}/${fmt(dC.getMonth()+1)} - ${fmt(dE.getDate())}/${fmt(dE.getMonth()+1)}`;
                }
                /* Series: cru vira linha; balde vira linha media + banda
                 * min-max pintada pelo plugin. Umidade e pressao mantem a cor
                 * fixa da grandeza e ganham tracejado por sensor quando ha
                 * mais de um - identidade nunca so pela cor. */
                const selOrder = _selectedSensors.map(Number);
                const ordIdx = sl => { const i = selOrder.indexOf(sl); return i < 0 ? 99 : i; };
                res.series.sort((a, b) => (ordIdx(a.slot) - ordIdx(b.slot)) || (a.kind - b.kind));
                const datasets = [];
                let hasAnyHum = false, hasAnyPress = false;
                const humDash = [[6, 3], [2, 3], [9, 3], [3, 6]];
                const pressDash = [[2, 2], [6, 2], [2, 6], [8, 3]];
                let humSeen = 0, pressSeen = 0;
                res.series.forEach(s => {
                    const idx = Math.max(0, selOrder.indexOf(s.slot));
                    let color, dash, axis = 'y', suffix = 'T (°C)';
                    if (s.kind === 2) {
                        color = _humColor; axis = 'y1'; suffix = 'H (%)';
                        dash = humDash[humSeen++ % humDash.length]; hasAnyHum = true;
                    } else if (s.kind === 3) {
                        color = _pressColor; axis = 'y2'; suffix = 'P (hPa)';
                        dash = pressDash[pressSeen++ % pressDash.length]; hasAnyPress = true;
                    } else {
                        color = _seriesColor(idx, false);
                    }
                    const ds = {
                        label: `${s.hwId} ${suffix}`,
                        borderColor: color, backgroundColor: 'transparent',
                        borderWidth: 1.6, borderDash: dash, tension: 0,
                        pointRadius: _h5IsolatedRadius, pointHitRadius: 8,
                        yAxisID: axis, spanGaps: false, _h5kind: s.kind
                    };
                    if (s.dec.mode === 'band') {
                        ds.data = s.dec.mid;
                        ds._h5BandLo = s.dec.lo;
                        ds._h5BandHi = s.dec.hi;
                        ds._h5BandColor = _h5Alpha(color, 0.18);
                    } else {
                        ds.data = s.dec.pts;
                    }
                    datasets.push(ds);
                });
                /* Extremos medidos sobre TODO registro da janela, na mesma
                 * passada da montagem - nunca discordam do desenho. Faixa de
                 * badges so com um sensor: com varios, min e max de sensores
                 * misturados nao significam nada. */
                const oneSensor = (selOrder.length === 1);
                gridBox.style.display = oneSensor ? '' : 'none';
                renderExtremes({ extremes: res.extremes });
                gridBox.style.opacity = '1';
                renderChart(datasets, hasAnyHum, hasAnyPress, cutoffEpoch * 1000, endEpoch * 1000, rangeIdx);
                hideOverlay();
            } catch (e) {
                if (_gchGen !== myGen) return;
                showProgress(false);
                showOverlay(window.t('hist_conn_lost', 'Connection lost.'));
            }
        }

        /* MAX/MIN badges, two per channel the server measured.
         *
         * The server sends extremes{} keyed by channel, measured over every
         * record in range before decimation — so a badge never disagrees with
         * the drawn series the way a browser-side scan of the decimated points
         * would. This replaced six hardcoded badges plus a show/hide branch per
         * quantity; pressure spent months being measured with no badge to
         * arrive in. Falls back to the fixed minT/maxT keys so a page served
         * from cache against a newer firmware, or vice versa, still shows
         * something. */
        function renderExtremes(json) {
            const box = document.getElementById('statsGrid');
            if (!box) return;
            let ex = json.extremes;
            if (!ex) {
                ex = {};
                if (json.minT !== undefined) ex.temp  = { min: json.minT, max: json.maxT, unit: '°C',  label: 'ch_temp'  };
                if (json.minH !== undefined) ex.hum   = { min: json.minH, max: json.maxH, unit: '%',   label: 'ch_hum'   };
                if (json.minP !== undefined) ex.press = { min: json.minP, max: json.maxP, unit: 'hPa', label: 'ch_press' };
            }
            const esc = s => String(s).replace(/[<>&"]/g, ch =>
                ({ '<': '&lt;', '>': '&gt;', '&': '&amp;', '"': '&quot;' }[ch]));
            /* Temperature keeps the warm colour it always had; everything else
               is cold. Purely cosmetic, and the only place channel identity
               still shows up on this page. */
            let html = '';
            Object.keys(ex).forEach(key => {
                const e = ex[key];
                if (!e || typeof e.max !== 'number') return;
                const tone = (key === 'temp') ? 'hot' : 'cold';
                const name = esc(window.t(e.label || ('ch_' + key), key.toUpperCase()));
                const unit = esc(e.unit || '');
                html += '<div class="stat-badge"><span>MAX ' + name + ':</span> <div class="' + tone + '">'
                     +  e.max.toFixed(1) + ' ' + unit + '</div></div>'
                     +  '<div class="stat-badge"><span>MIN ' + name + ':</span> <div class="' + tone + '">'
                     +  e.min.toFixed(1) + ' ' + unit + '</div></div>';
            });
            box.innerHTML = html || '<div class="stat-badge"><span>--</span></div>';
        }

        /* Step e formato dos ticks por range — define grade do eixo X. */
        const _xStepMsByRange = [
            10*60*1000,        /* 1h: 10min */
            60*60*1000,        /* 6h: 1h */
            4*60*60*1000,      /* 24h: 4h */
            24*60*60*1000,     /* 7d: 1 dia */
            5*24*60*60*1000,   /* 1M: 5 dias */
            30*24*60*60*1000,  /* 1A: ~mês (12 ticks) */
            null               /* MAX: auto */
        ];
        function _xTickLabel(ms, rangeIdx) {
            const d = new Date(ms);
            const p = n => String(n).padStart(2, '0');
            if (rangeIdx <= 2) {
                /* 1h, 6h, 24h: HH:MM */
                return p(d.getHours()) + ':' + p(d.getMinutes());
            }
            if (rangeIdx === 3 || rangeIdx === 4) {
                /* 7d, 1M: DD/MM */
                return p(d.getDate()) + '/' + p(d.getMonth()+1);
            }
            if (rangeIdx === 5) {
                /* 1A: mês abreviado */
                const m = ['Jan','Fev','Mar','Abr','Mai','Jun','Jul','Ago','Set','Out','Nov','Dez'];
                return m[d.getMonth()] + '/' + String(d.getFullYear()).slice(2);
            }
            /* MAX */
            return p(d.getDate()) + '/' + p(d.getMonth()+1) + '/' + String(d.getFullYear()).slice(2);
        }

        function _h5Alpha(hex, a) {
            const r = parseInt(hex.slice(1, 3), 16), g = parseInt(hex.slice(3, 5), 16),
                  b = parseInt(hex.slice(5, 7), 16);
            return 'rgba(' + r + ',' + g + ',' + b + ',' + a + ')';
        }

        /* Ponto isolado (vizinhos nulos dos dois lados) ganha raio visivel;
         * sem isso uma amostra unica entre duas lacunas nao desenha nada e
         * parece dado perdido. */
        function _h5IsolatedRadius(c) {
            const d = c.dataset.data, i = c.dataIndex;
            if (!d || !d[i] || d[i].y === null || d[i].y === undefined) return 0;
            const pn = (i === 0) || !d[i - 1] || d[i - 1].y === null || d[i - 1].y === undefined;
            const nn = (i === d.length - 1) || !d[i + 1] || d[i + 1].y === null || d[i + 1].y === undefined;
            return (pn && nn) ? 2.4 : 0;
        }

        /* Banda min-max por tras da linha media, com recorte na area do
         * grafico e respeito as lacunas (null quebra o poligono). Plugin
         * local em vez de datasets extras: a legenda, o tooltip e o modo
         * nearest so enxergam as linhas de verdade. */
        const _h5BandPlugin = {
            id: 'h5band',
            beforeDatasetsDraw(chart) {
                const area = chart.chartArea;
                if (!area) return;
                const c = chart.ctx;
                chart.data.datasets.forEach((ds, di) => {
                    if (!ds._h5BandLo || !chart.isDatasetVisible(di)) return;
                    const xs = chart.scales.x, ys = chart.scales[ds.yAxisID || 'y'];
                    if (!xs || !ys) return;
                    c.save();
                    c.beginPath();
                    c.rect(area.left, area.top, area.right - area.left, area.bottom - area.top);
                    c.clip();
                    c.fillStyle = ds._h5BandColor || 'rgba(128,128,128,0.15)';
                    const lo = ds._h5BandLo, hi = ds._h5BandHi;
                    let seg = [];
                    const flush = () => {
                        if (seg.length >= 2) {
                            c.beginPath();
                            seg.forEach((p, i) => {
                                const X = xs.getPixelForValue(p.x), Y = ys.getPixelForValue(p.hi);
                                if (i === 0) c.moveTo(X, Y); else c.lineTo(X, Y);
                            });
                            for (let i = seg.length - 1; i >= 0; i--) {
                                c.lineTo(xs.getPixelForValue(seg[i].x), ys.getPixelForValue(seg[i].lo));
                            }
                            c.closePath();
                            c.fill();
                        }
                        seg = [];
                    };
                    for (let i = 0; i < lo.length; i++) {
                        if (lo[i].y === null || lo[i].y === undefined
                            || hi[i].y === null || hi[i].y === undefined) { flush(); continue; }
                        seg.push({ x: lo[i].x, lo: lo[i].y, hi: hi[i].y });
                    }
                    flush();
                    c.restore();
                });
            }
        };

        function renderChart(datasets, showHumAxis, showPressAxis, xMin, xMax, rangeIdx) {
            const ctx = document.getElementById('myChart').getContext('2d');
            if (myChart) myChart.destroy();
            const step = _xStepMsByRange[rangeIdx] || undefined;
            myChart = new Chart(ctx, {
                type: 'line',
                data: { datasets: datasets },
                plugins: [_h5BandPlugin],
                options: {
                    animation: false, responsive: true, maintainAspectRatio: false,
                    interaction: { mode: 'nearest', axis: 'x', intersect: false },
                    plugins: {
                        legend: { display: datasets.length > 1, position: 'top', labels: { color: '#a1a1aa', font: { size: 11 } } },
                        tooltip: {
                            callbacks: {
                                /* X e epoch ms - formata como data/hora local. */
                                title: function(items) {
                                    if (!items || !items.length) return '';
                                    const d = new Date(items[0].parsed.x);
                                    const p = n => String(n).padStart(2, '0');
                                    return p(d.getDate()) + '/' + p(d.getMonth()+1) + '/' + d.getFullYear() +
                                           ' ' + p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds());
                                },
                                label: function(item) {
                                    const d = item.dataset;
                                    const dec = (d._h5kind === 1 || d._h5kind === undefined) ? 2 : 1;
                                    let txt = ' ' + d.label + ': ' + item.parsed.y.toFixed(dec);
                                    if (d._h5BandLo) {
                                        const lo = d._h5BandLo[item.dataIndex], hi = d._h5BandHi[item.dataIndex];
                                        if (lo && hi && lo.y !== null && hi.y !== null && (hi.y - lo.y) > 0)
                                            txt += '  [' + lo.y.toFixed(dec) + ' … ' + hi.y.toFixed(dec) + ']';
                                    }
                                    return txt;
                                }
                            }
                        }
                    },
                    scales: {
                        x: {
                            type: 'linear',
                            min: xMin, max: xMax,    /* largura = periodo pedido (mesmo sem dados) */
                            ticks: {
                                color: _tickColor(),
                                stepSize: step,
                                maxTicksLimit: 13,
                                callback: function(value) { return _xTickLabel(value, rangeIdx); },
                                autoSkip: true
                            },
                            grid: { color: _gridColor() }
                        },
                        y: { type: 'linear', display: true, position: 'left', ticks: { color: '#e5484d' }, grid: { color: _gridColor() }, title: { display:true, text:'°C', color:'#e5484d', font:{size:10} } },
                        y1: { type: 'linear', display: showHumAxis, position: 'right', ticks: { color: _humColor }, grid: { drawOnChartArea: false }, title: { display:true, text:'%RH', color:_humColor, font:{size:10} } },
                        /* Third axis: pressure sits around 1000 hPa and would flatten
                           both %RH and °C into straight lines if it shared either. */
                        y2: { type: 'linear', display: showPressAxis, position: 'right', ticks: { color: _pressColor }, grid: { drawOnChartArea: false }, title: { display:true, text:'hPa', color:_pressColor, font:{size:10} } }
                    }
                }
            });
        }

        // Logs — binary parsing in browser. Tabelas sincronizadas com LogManager::translateCode (LogManager.cpp).
        const EVT_NAMES_EN = { '0':'OK', '1':'System boot', '2':'User-requested reboot', '3':'Heap memory low', '4':'Uptime milestone', '5':'Routine log records suppressed', '10':'WiFi connecting', '11':'WiFi disconnected', '12':'WiFi scanning', '13':'NTP synced', '14':'IP acquired', '15':'AP mode started', '20':'Storage failure', '21':'Config saved', '22':'Storage rotated', '23':'Flash formatting', '24':'Storage recovered', '25':'Config migrated', '30':'Telemetry sent', '31':'Telemetry failed', '32':'Telemetry retry', '33':'Telemetry queued', '34':'SSL cert loaded', '35':'MQTT connected', '36':'MQTT disconnected', '37':'MQTT published', '100':'Sensor recovered', '101':'Sensor timeout', '102':'Sensor checksum error', '103':'Sensor CRC error', '104':'Sensor out of range', '105':'Hardware mismatch', '106':'Sensor missing', '200':'Touch event', '201':'Display restarted', '202':'Graph rendered', '300':'Login success', '301':'Login failed', '302':'Unauthorized access', '303':'Config changed', '304':'Session expired', '305':'File uploaded', '306':'File deleted', '400':'Display launched on Core 1', '401':'Initial touch cal saved', '402':'Touch calibration required', '403':'AP mode triggered by user', '404':'System ready', '405':'System ready (AP mode)', '406':'Storage critical failure', '407':'Sensors calibrated', '408':'NTP correcting timestamps', '409':'Timestamps corrected', '410':'Graph caches invalidated', '440':'Theme changed via UI', '441':'Language changed via UI', '442':'Alarm limits saved via UI', '443':'Touch cal saved to flash', '444':'Touch sensitivity saved', '445':'Display PIN changed', '446':'Sound settings saved', '447':'Alarm silenced via UI', '448':'Alarm silence expired', '449':'All alarms deactivated (RAM)', '470':'Alarm triggered', '471':'Alarm cleared', '472':'Alarm silence cancelled', '480':'Min/Max cache loaded', '481':'Min/Max cache partial', '482':'Graph cache refresh started', '483':'Graph cache refresh done', '484':'Graph cache: ambient', '485':'Graph cache: board temp', '486':'Graph cache preload done', '487':'Graph loading', '488':'Graph render budget exceeded', '489':'Preload budget exceeded', '500':'Display pause stuck >5s', '501':'Yield stuck >10s', '502':'Core 1 dead >10s, restarting', '503':'Flash busy collision', '510':'History record saved', '511':'Heap status report', '512':'History skip: no time reference', '513':'History resumed: time reference acquired', '514':'History skip: V4 schema is empty', '515':'History skip: schema covers no active sensor', '520':'DHCP mode enabled', '521':'Static IP mode enabled', '522':'WiFi manager starting', '523':'WiFi SSID not configured', '524':'Provisional time set from flash', '525':'WiFi connect timeout', '526':'WiFi dormant mode', '527':'Show IP', '528':'mDNS start failed', '540':'HTTP transport initialized', '541':'MQTT transport initialized', '542':'MQTT connecting', '543':'cert.pem empty, insecure mode', '544':'cert.pem read error', '545':'No cert.pem, insecure mode', '546':'Forcing telemetry sync', '547':'Retry logs suppressed', '560':'History write failed', '561':'Timestamp correction budget exceeded', '562':'Storage limit budget exceeded', '563':'Skipping active log file', '564':'Storage stats report', '565':'Config report', '566':'History block sealed', '567':'History snapshot written', '568':'History schema mismatch', '569':'Legacy history purged', '570':'Web server started', '571':'Client disconnected (file)', '572':'Client disconnected (history)', '573':'Screenshot aborted by client', '574':'File uploaded', '575':'Client disconnected (broken pipe)', '576':'Web cert invalid (HTTP fallback)', '580':'Theme applied', '581':'Theme not found', '585':'Unknown command', '590':'Runtime sensors loaded', '600':'Force unpause', '999':'Unknown error' };
        const EVT_NAMES_PT = { '0':'OK', '1':'Boot do sistema', '2':'Reboot solicitado pelo usuario', '3':'Heap baixa', '4':'Marco de uptime', '5':'Registros de rotina suprimidos', '10':'Conectando WiFi', '11':'WiFi desconectado', '12':'Varredura WiFi', '13':'NTP sincronizado', '14':'IP obtido', '15':'AP iniciado', '20':'Falha no storage', '21':'Config salva', '22':'Storage rotacionado', '23':'Formatando flash', '24':'Storage recuperado', '25':'Config migrada', '30':'Telemetria enviada', '31':'Falha de telemetria', '32':'Retry de telemetria', '33':'Telemetria enfileirada', '34':'Cert SSL carregado', '35':'MQTT conectado', '36':'MQTT desconectado', '37':'MQTT publicado', '100':'Sensor recuperado', '101':'Timeout de sensor', '102':'Erro de checksum', '103':'Erro de CRC', '104':'Sensor fora de range', '105':'Divergencia de hardware', '106':'Sensor ausente', '200':'Evento de toque', '201':'Display reiniciado', '202':'Grafico renderizado', '300':'Login bem-sucedido', '301':'Falha de login', '302':'Acesso nao autorizado', '303':'Config alterada', '304':'Sessao expirada', '305':'Arquivo enviado', '306':'Arquivo apagado', '400':'Display iniciado no Core 1', '401':'Calibracao inicial do touch salva', '402':'Calibracao do touch necessaria', '403':'AP ativado pelo usuario', '404':'Sistema pronto', '405':'Sistema pronto (modo AP)', '406':'Falha critica de storage', '407':'Sensores calibrados', '408':'NTP corrigindo timestamps', '409':'Timestamps corrigidos', '410':'Caches de grafico invalidados', '440':'Tema alterado via UI', '441':'Idioma alterado via UI', '442':'Limites de alarme salvos via UI', '443':'Calibracao do touch salva', '444':'Sensibilidade do touch salva', '445':'PIN do display alterado', '446':'Config de som salva', '447':'Alarme silenciado via UI', '448':'Silenciamento de alarme expirou', '449':'Todos alarmes desativados (RAM)', '470':'Alarme disparado', '471':'Alarme zerado', '472':'Silenciamento cancelado', '480':'Cache Min/Max carregado', '481':'Cache Min/Max parcial', '482':'Refresh de cache iniciado', '483':'Refresh de cache concluido', '484':'Cache de grafico: ambiente', '485':'Cache de grafico: placa', '486':'Pre-carga de cache concluida', '487':'Carregando grafico', '488':'Budget de render excedido', '489':'Budget de pre-carga excedido', '500':'Pause do display preso >5s', '501':'Yield preso >10s', '502':'Core 1 travado >10s, reiniciando', '503':'Colisao por flash ocupado', '510':'Registro de historico salvo', '511':'Relatorio de heap', '512':'Historico pulado: sem referencia de hora', '513':'Historico retomado: referencia de hora obtida', '514':'Historico pulado: schema V4 vazio', '515':'Historico pulado: schema nao cobre sensor ativo', '520':'Modo DHCP ativado', '521':'Modo IP estatico ativado', '522':'Gerenciador WiFi iniciando', '523':'SSID WiFi nao configurado', '524':'Hora provisoria do flash', '525':'Timeout na conexao WiFi', '526':'WiFi em modo dormente', '527':'Mostrar IP', '528':'Falha ao iniciar mDNS', '540':'Transporte HTTP inicializado', '541':'Transporte MQTT inicializado', '542':'MQTT conectando', '543':'cert.pem vazio, modo inseguro', '544':'Erro de leitura de cert.pem', '545':'Sem cert.pem, modo inseguro', '546':'Forcando sync de telemetria', '547':'Logs de retry suprimidos', '560':'Falha em escrever historico', '561':'Budget de correcao de ts excedido', '562':'Budget de limite de storage excedido', '563':'Pulando arquivo de log ativo', '564':'Relatorio de estatisticas', '565':'Relatorio de config', '566':'Bloco de historico selado', '567':'Snapshot de historico gravado', '568':'Schema de historico divergente', '569':'Historico legado apagado', '570':'Servidor web iniciado', '571':'Cliente desconectado (arquivo)', '572':'Cliente desconectado (historico)', '573':'Screenshot abortado pelo cliente', '574':'Arquivo enviado', '575':'Cliente desconectado (conexao encerrada)', '576':'Cert web invalido (HTTP)', '580':'Tema aplicado', '581':'Tema nao encontrado', '585':'Comando desconhecido', '590':'Sensores em runtime carregados', '600':'Forcar despausar', '999':'Erro desconhecido' };
        function evtName(code) { let l = localStorage.getItem('simut_lang') || 'en'; let dict = (l === 'pt') ? EVT_NAMES_PT : EVT_NAMES_EN; let lbl = dict[code.toString()]; if (lbl) return lbl; return (l === 'pt' ? 'Evento #' : 'Event #') + code; }
        const TAG_NAMES = ['APP','NET','TEL','STO','WEB','CFG','CLI','SENSOR','HIST','SYS','DSP','SEC','OTA','?','?','?'];
        const LVL_LABELS = ['DBG','INF','WRN','ERR','FTL']; const LVL_CLASS = ['log-inf','log-inf','log-wrn','log-err','log-err'];
        const LVL_NUM = { 'INF':1, 'WRN':2, 'ERR':3 };
        function fmtUptime(ms) { let s=Math.floor(ms/1000); let d=Math.floor(s/86400); s%=86400; let h=Math.floor(s/3600); s%=3600; let m=Math.floor(s/60); s%=60; let p=v=>v<10?'0'+v:v; return d>0?d+'d '+p(h)+':'+p(m)+':'+p(s):p(h)+':'+p(m)+':'+p(s); }

        let parsedLogRows = [];

        async function loadLogs() {
            const tbody = document.getElementById('logBody');
            const btn = document.getElementById('btnLoadLogs');
            const pWrap = document.getElementById('logProgressWrap');
            const pFill = document.getElementById('logProgFill');
            const pDet = document.getElementById('logProgDetail');

            btn.disabled = true;
            pWrap.style.display = 'block';
            pFill.style.width = '0%';
            pFill.classList.add('pulse');
            document.getElementById('logProgStatus').innerText = window.t('hist_loading', 'Loading...');

            try {
                const res = await fetchSafe('/api/logs', {timeout: 30000, retries: 1});
                if(!res.ok) throw new Error("HTTP");

                /* Recebe bytes binários brutos (12 bytes por registro) */
                const reader = res.body.getReader();
                let chunks = []; let received = 0;
                const estTotal = 20000;

                while (true) {
                    const { done, value } = await reader.read();
                    if (done) break;
                    chunks.push(value);
                    received += value.length;
                    let pct = Math.min(95, (received / estTotal) * 100);
                    pFill.style.width = pct + '%';
                    pDet.innerText = (received / 1024).toFixed(1) + ' KB';
                }

                /* Concatena chunks em ArrayBuffer */
                let totalLen = chunks.reduce((s, c) => s + c.length, 0);
                let buf = new Uint8Array(totalLen);
                let offset = 0;
                for (const c of chunks) { buf.set(c, offset); offset += c.length; }

                pFill.classList.remove('pulse');
                pFill.style.width = '100%';
                document.getElementById('logProgStatus').innerText = window.t('hist_done', 'Complete');
                setTimeout(() => pWrap.style.display = 'none', 600);

                /* Parseia registros binários de 12 bytes — little-endian (ARM Cortex-M0+).
                 * Uptime é SEGUNDOS em 24 bits: [4..5] baixo, [11] alto. Era um
                 * uint16 de HORAS em [4..5] com [11] reservado, e por isso a
                 * coluna lia zero em todo registro de uma placa que reinicia mais
                 * de uma vez por hora — ou seja, em todos. */
                const dv = new DataView(buf.buffer);
                const recSize = 12;
                const numRecs = Math.floor(totalLen / recSize);
                parsedLogRows = [];

                for (let i = 0; i < numRecs; i++) {
                    const off = i * recSize;
                    const epoch    = dv.getUint32(off, true);
                    const upSec    = dv.getUint16(off + 4, true) | (dv.getUint8(off + 11) << 16);
                    const code     = dv.getUint16(off + 6, true);
                    const ctx      = dv.getInt16(off + 8, true);
                    const flags    = dv.getUint8(off + 10);

                    const lvl   = (flags >> 5) & 0x07;
                    const core  = (flags >> 4) & 0x01;
                    const tagId = flags & 0x0F;

                    let dateStr;
                    if (epoch > 1000000000) {
                        let dt = new Date(epoch * 1000);
                        dateStr = fmt(dt.getDate())+'/'+fmt(dt.getMonth()+1)+'/'+dt.getFullYear()+' '+fmt(dt.getHours())+':'+fmt(dt.getMinutes())+':'+fmt(dt.getSeconds());
                    } else {
                        dateStr = 'Boot +' + fmtUptime(upSec * 1000);
                    }
                    let upStr = fmtUptime(upSec * 1000);
                    let lvlLabel = LVL_LABELS[lvl] || 'UNK';
                    let lvlCls = LVL_CLASS[lvl] || '';
                    let tag = TAG_NAMES[tagId] || '?';
                    let desc = evtName(code) + (ctx !== 0 ? ' <span style="color:var(--sub)">[ctx: ' + ctx + ']</span>' : '');

                    parsedLogRows.push({ epoch, upSec, code, ctx, lvl, dateStr, upStr, lvlLabel, lvlCls, tag, desc });
                }

                renderLogTable();

                if(!logsLoadedOnce) {
                    logsLoadedOnce = true;
                    btn.setAttribute('data-i18n', 'hist_ref');
                    btn.innerText = window.t('hist_ref', 'Refresh');
                    btn.style.color = "var(--txt)";
                    btn.style.borderColor = "var(--border)";
                }
            } catch(e) {
                pWrap.style.display = 'none';
                tbody.innerHTML = `<tr><td colspan="5" class="log-err" style="text-align:center">Error fetching logs</td></tr>`;
            }
            btn.disabled = false;
        }

        function renderLogTable() {
            const tbody = document.getElementById('logBody');
            let html = '';
            for (let i = parsedLogRows.length - 1; i >= 0; i--) {
                const r = parsedLogRows[i];
                html += `<tr class="log-row" data-lvl="${r.lvl}"><td>${r.dateStr}</td><td style="color:var(--sub)">${r.upStr}</td><td class="${r.lvlCls}">${r.lvlLabel}</td><td style="color:var(--acc)">${r.tag}</td><td style="color:var(--txt)">${r.desc}</td></tr>`;
            }
            tbody.innerHTML = html || `<tr><td colspan="5" style="text-align:center">${window.t('hist_none', 'No events.')}</td></tr>`;
            filterLogs();
        }

        function filterLogs() {
            let input = document.getElementById('logSearch').value.toLowerCase();
            let showInf = document.getElementById('chkInf').checked;
            let showWrn = document.getElementById('chkWrn').checked;
            let showErr = document.getElementById('chkErr').checked;
            document.querySelectorAll('.log-row').forEach(row => {
                let lvl = parseInt(row.getAttribute('data-lvl'));
                let lvlOk = false;
                if (lvl <= 1 && showInf) lvlOk = true;   /* DBG + INF */
                if (lvl === 2 && showWrn) lvlOk = true;   /* WRN */
                if (lvl >= 3 && showErr) lvlOk = true;     /* ERR + FTL */
                let textOk = (input.length === 0 || row.innerText.toLowerCase().includes(input));
                row.style.display = (lvlOk && textOk) ? '' : 'none';
            });
        }
        async function clearLogs() { if(!confirm(window.t('hist_clear_msg', 'Clear all?'))) return; try { let r = await fetchSafe('/api/clear_logs', {method: 'POST'}); if (r.status === 503) { showToast(window.t('display_busy','Display in use. Try again shortly.'), 'warn'); return; } if (!r.ok) { showToast(window.t('hist_clear_err','Failed to clear logs.'), 'err'); return; } showToast(window.t('hist_cleared','Logs cleared.'), 'ok'); if(logsLoadedOnce) loadLogs(); } catch(e) { showToast(window.t('net_conn_err','Connection error.'), 'err'); } }

        window.onLangChange = function() { renderCalendar(); if(logsLoadedOnce) { loadLogs(); } else { let btn = document.getElementById('btnLoadLogs'); btn.innerText = window.t(btn.getAttribute('data-i18n'), 'Load'); } };


        /* ============== F-CSV: export histórico + logs (UI simplificada) ============== */
        /* Mini CRC32-IEEE com tabela 256. Compat com firmware crc32_*. */
        const _crcTab = (() => { const t = new Uint32Array(256);
            for (let i=0;i<256;i++){let c=i; for(let j=0;j<8;j++) c=(c&1)?((c>>>1)^0xEDB88320):(c>>>1); t[i]=c>>>0;} return t; })();
        function crc32(u8) { let c = 0xFFFFFFFF >>> 0;
            for (let i=0;i<u8.length;i++) c = (_crcTab[(c ^ u8[i]) & 0xFF] ^ (c >>> 8)) >>> 0;
            return (~c) >>> 0; }

        function _isoLocal(epoch) {
            const d = new Date(epoch * 1000);
            const tz = -d.getTimezoneOffset();
            const sgn = tz >= 0 ? '+' : '-';
            const tzh = String(Math.floor(Math.abs(tz)/60)).padStart(2,'0');
            const tzm = String(Math.abs(tz)%60).padStart(2,'0');
            const p = n => String(n).padStart(2,'0');
            return d.getFullYear() + '-' + p(d.getMonth()+1) + '-' + p(d.getDate()) +
                   'T' + p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds()) +
                   sgn + tzh + ':' + tzm;
        }

        function _readUtf8(u8, off, len) {
            try { return new TextDecoder('utf-8', {fatal:false}).decode(u8.subarray(off, off+len)); }
            catch(e) { let s=''; for(let i=0;i<len;i++) s+=String.fromCharCode(u8[off+i]); return s; }
        }

        /* Decoder .simx kind='H' -> array de linhas CSV (sem header).
         * Filtra por sensor selecionado (sensorIdx == 'all' ou string com idx). */
        /* HistoryV5 decoder for the browser.
         *
         * The export used to arrive as a .simx bundle of 70-byte BinaryHistoryRecords
         * that the device built by decoding its own .h5 files. On this bench that was
         * 70.1 B per record on the wire against 5.63 B on flash — 12.5x — because the
         * record is 33 fixed channels and only 6 exist, so 77 % of every one of them
         * was the NAN sentinel.
         *
         * With this, the page downloads the .h5 files themselves through /download and
         * decodes them here. The device does a file read and nothing else.
         *
         * Mirrors src/HistoryV5.cpp and tools/history_v5.py; the format is specified
         * in docs/HistoryV5_Instrucoes_Implementacao.md.
         */

        const H5_MAGIC = 0x4835;
        const H5_VERSION = 0x02;
        const H5_CHUNK_SCHEMA = 0x01;
        const H5_CHUNK_DATA = 0x02;
        const H5_FLAG_RAW = 0x01;
        const H5_NAN = -32768;

        const H5_KIND_TEMP = 0x01, H5_KIND_HUM = 0x02, H5_KIND_PRESS = 0x03;

        /* CRC-16/CCITT-FALSE, nibble table — same one the firmware carries. */
        const _H5_CRCTAB = [0x0000,0x1021,0x2042,0x3063,0x4084,0x50A5,0x60C6,0x70E7,
                            0x8108,0x9129,0xA14A,0xB16B,0xC18C,0xD1AD,0xE1CE,0xF1EF];

        function h5Crc16(u8, from, len, seed) {
            let crc = (seed === undefined) ? 0xFFFF : seed;
            for (let i = 0; i < len; i++) {
                crc ^= u8[from + i] << 8;
                crc = ((crc << 4) ^ _H5_CRCTAB[(crc >> 12) & 0x0F]) & 0xFFFF;
                crc = ((crc << 4) ^ _H5_CRCTAB[(crc >> 12) & 0x0F]) & 0xFFFF;
            }
            return crc & 0xFFFF;
        }

        function _h5ToI16(v) { v &= 0xFFFF; return (v & 0x8000) ? v - 0x10000 : v; }

        /* MSB-first bit reader. Multiplication rather than shifts: a 32-bit resync
         * epoch overflows the sign bit of the shift operators. */
        function _h5Bits(u8, from, len) {
            let pos = 0;
            return {
                get(width) {
                    let v = 0;
                    for (let i = 0; i < width; i++) {
                        const bi = pos >> 3;
                        const bit = (bi < len) ? ((u8[from + bi] >> (7 - (pos & 7))) & 1) : 0;
                        v = (v * 2) + bit;
                        pos++;
                    }
                    return v;
                }
            };
        }

        /**
         * Decode one .h5 file.
         * @param {ArrayBuffer|Uint8Array} buf
         * @param {number} nominalSecs sampling interval; the file does not carry it.
         * @returns {{blocks:number, rejected:number, series:Array<{schema:Array,t:number,v:Int16Array}>}}
         */
        function h5Decode(buf, nominalSecs) {
            const u8 = (buf instanceof Uint8Array) ? buf : new Uint8Array(buf);
            const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
            const nominal = nominalSecs > 0 ? nominalSecs : 60;
            const out = { blocks: 0, rejected: 0, series: [] };
            let schema = null;
            let pos = 0;

            while (pos + 8 <= u8.length) {
                const magic = dv.getUint16(pos, true);
                if (magic !== H5_MAGIC || u8[pos + 2] !== H5_VERSION) { out.rejected++; break; }
                const type = u8[pos + 3], flags = u8[pos + 4], a = u8[pos + 5], b = u8[pos + 6];

                if (type === H5_CHUNK_SCHEMA) {
                    const n = a;
                    const size = 8 + 4 * n + 2;
                    if (n === 0 || n > 16 || pos + size > u8.length) { out.rejected++; break; }
                    const stored = dv.getUint16(pos + 8 + 4 * n, true);
                    if (h5Crc16(u8, pos, 8 + 4 * n) !== stored) { out.rejected++; pos += size; continue; }
                    schema = [];
                    for (let i = 0; i < n; i++) {
                        const o = pos + 8 + 4 * i;
                        schema.push({ id: u8[o], kind: u8[o + 1],
                                      scaleExp: _h5ToI16(u8[o + 2] | (u8[o + 2] & 0x80 ? 0xFF00 : 0)) });
                    }
                    pos += size;
                    continue;
                }
                if (type !== H5_CHUNK_DATA) { out.rejected++; break; }

                const n = b;
                const hdrLen = 16 + 6 * n;
                if (n === 0 || n > 16 || a === 0 || a > 60 || pos + hdrLen > u8.length) { out.rejected++; break; }
                const count = a;
                const t0 = dv.getUint32(pos + 8, true);
                const payLen = dv.getUint16(pos + 12, true);
                const crcExp = dv.getUint16(pos + 14, true);
                const total = hdrLen + payLen;
                if (pos + total > u8.length) { out.rejected++; break; }

                /* The stored CRC covers everything but its own two bytes. */
                let crc = h5Crc16(u8, pos, 14);
                crc = h5Crc16(u8, pos + 16, total - 16, crc);
                if (crc !== crcExp) { out.rejected++; pos += total; continue; }
                if (!schema || schema.length !== n) { out.rejected++; pos += total; continue; }

                const kf = new Int16Array(n);
                for (let c = 0; c < n; c++) kf[c] = dv.getInt16(pos + 16 + 2 * c, true);

                const prev = Int16Array.from(kf);
                out.series.push({ schema, t: t0, v: Int16Array.from(kf) });
                out.blocks++;

                if (count > 1) {
                    if (flags & H5_FLAG_RAW) {
                        const rec = 2 + 2 * n;
                        for (let r = 0; r < count - 1; r++) {
                            const o = pos + hdrLen + r * rec;
                            const vals = new Int16Array(n);
                            for (let c = 0; c < n; c++) vals[c] = dv.getInt16(o + 2 + 2 * c, true);
                            out.series.push({ schema, t: t0 + dv.getUint16(o, true), v: vals });
                        }
                    } else {
                        const br = _h5Bits(u8, pos + hdrLen, payLen);
                        let prevDelta = nominal, prevEpoch = t0;
                        for (let r = 1; r < count; r++) {
                            let epoch;
                            if (br.get(1) === 0) {
                                epoch = prevEpoch + prevDelta;
                            } else if (br.get(1) === 0) {
                                let d = br.get(7); if (d & 0x40) d -= 128;
                                prevDelta += d; epoch = prevEpoch + prevDelta;
                            } else if (br.get(1) === 0) {
                                let d = br.get(12); if (d & 0x800) d -= 4096;
                                prevDelta += d; epoch = prevEpoch + prevDelta;
                            } else {
                                epoch = br.get(32);
                                prevDelta = nominal;
                            }
                            prevEpoch = epoch;

                            const vals = new Int16Array(n);
                            for (let c = 0; c < n; c++) {
                                let val;
                                if (br.get(1) === 0)      val = prev[c];
                                else if (br.get(1) === 0) val = prev[c] + _h5Unzig(br.get(3));
                                else if (br.get(1) === 0) val = prev[c] + _h5Unzig(br.get(6));
                                else if (br.get(1) === 0) val = prev[c] + _h5Unzig(br.get(10));
                                else                      val = _h5ToI16(br.get(16));
                                val = _h5ToI16(val);
                                prev[c] = val; vals[c] = val;
                            }
                            out.series.push({ schema, t: epoch, v: vals });
                        }
                    }
                }
                pos += total;
            }
            return out;
        }

        function _h5Unzig(z) {
            const v = ((z >>> 1) ^ (-(z & 1))) & 0xFFFF;
            return (v & 0x8000) ? v - 0x10000 : v;
        }

        /* Unit and display precision per physical quantity. Kept as the page always
         * showed them so the CSV does not change shape: humidity keeps two decimals
         * even though V5 stores it at one. */
        function h5KindUnit(kind) {
            if (kind === H5_KIND_TEMP)  return '°C';
            if (kind === H5_KIND_HUM)   return '%RH';
            if (kind === H5_KIND_PRESS) return 'hPa';
            return '';
        }
        function h5KindDecimals(kind) {
            if (kind === H5_KIND_PRESS) return 1;
            return 2;
        }

        /**
         * CSV rows, in the shape the export has always produced:
         *   timestamp_iso,sensor_id,sensor_name,value,unit
         *
         * A descriptor id is slot * 8 + channel, so the slot the sensor table is keyed
         * by comes straight out of the file — no string pool, and pressure finally
         * lands on the sensor that measured it instead of an empty column.
         */
        function h5ToCsvLines(decoded, slots, filterSet, from, to, isoFn) {
            const lines = [];
            for (const rec of decoded.series) {
                if (rec.t < from || rec.t > to) continue;
                const iso = isoFn(rec.t);
                for (let c = 0; c < rec.schema.length; c++) {
                    const raw = rec.v[c];
                    if (raw === H5_NAN) continue;
                    const d = rec.schema[c];
                    const slot = d.id >> 3;
                    if (filterSet && !filterSet.has(slot)) continue;
                    const s = slots[slot];
                    if (!s) continue;
                    const val = raw * Math.pow(10, d.scaleExp);
                    lines.push(iso + ',' + s.hwId + ',"' + s.friendly + '",'
                               + val.toFixed(h5KindDecimals(d.kind)) + ',' + h5KindUnit(d.kind));
                }
            }
            return lines;
        }
        function _decodeSimxHistory(buf, filterIdx) {
            const u8 = new Uint8Array(buf);
            if (u8.length < 36) throw new Error('blob too small');
            const dv = new DataView(buf);
            const magic = String.fromCharCode(u8[0],u8[1],u8[2],u8[3]);
            if (magic !== 'SIMX') throw new Error('bad magic');
            if (u8[4] !== 1) throw new Error('bad version');
            if (u8[5] !== 0x48 /*'H'*/) throw new Error('bad kind');
            /* Era um teste fixo `!== 28` contra um firmware que emitia 74 —
               todo export falhava aqui com "bad recordSize". Aceita o que o
               cabecalho declarar e usa esse passo na iteracao. */
            const recSize = dv.getUint16(8, true);
            if (!recSize || recSize > 512) throw new Error('bad recordSize');
            const tblSize = dv.getUint32(20, true);
            /* CRC32: ultimos 4 bytes vs computado */
            const crcExp = dv.getUint32(u8.length - 4, true);
            const crcCalc = crc32(u8.subarray(0, u8.length - 4));
            if (crcExp !== crcCalc) throw new Error('CRC32 mismatch');

            /* Parse SENSOR_TABLE -> map idx -> {hwId, friendly} */
            const sensors = {};
            let off = 32, end = 32 + tblSize;
            while (off < end) {
                const idx = u8[off++]; const hwLen = u8[off++];
                const hwId = _readUtf8(u8, off, hwLen); off += hwLen;
                const frLen = u8[off++];
                const friendly = _readUtf8(u8, off, frLen); off += frLen;
                sensors[idx] = { hwId, friendly };
            }

            /* Itera PAYLOAD: N x BinaryHistoryRecord (70 B):
             *   epoch u32 | sensors[16] i16 | humidity[16] i16 | pressure i16
             *
             * Este leitor estava parado num registro de 28 B (epoch + o par
             * ambiente + sensors[10]) enquanto o firmware ja emitia 74 B com
             * 16 slots, umidade por slot e pressao — lia a partir do offset
             * errado do segundo registro em diante. Agora acompanha o layout,
             * e o par ambiente saiu junto com o slot 10 especial.
             *
             * REC_SIZE vem do cabecalho (recordSize) quando presente, para o
             * proximo passo de layout nao voltar a silenciar este parser. */
            const REC = recSize && recSize > 0 ? recSize : 70;
            const OFF_T = 4, OFF_H = 4 + 32, OFF_P = 4 + 64;
            const payStart = 32 + tblSize;
            const payEnd = u8.length - 4;
            const lines = [];
            const NAN_S = -32768;
            /* filterIdx aceita: 'all' (string), array de slots [0,5], ou int legacy */
            const wantAll = (filterIdx === 'all');
            const filterSet = wantAll ? null : new Set(
                Array.isArray(filterIdx) ? filterIdx.map(Number) : [parseInt(filterIdx, 10)]
            );
            for (let p = payStart; p + REC <= payEnd; p += REC) {
                const epoch = dv.getUint32(p, true);
                const iso = _isoLocal(epoch);
                for (let i = 0; i < 16; i++) {
                    if (!wantAll && !filterSet.has(i)) continue;
                    const s = sensors[i];
                    if (!s) continue;
                    const t = dv.getInt16(p + OFF_T + i*2, true);
                    if (t !== NAN_S) {
                        lines.push(`${iso},${s.hwId},"${s.friendly}",${(t/100).toFixed(2)},°C`);
                    }
                    const h = dv.getInt16(p + OFF_H + i*2, true);
                    if (h !== NAN_S) {
                        lines.push(`${iso},${s.hwId},"${s.friendly}",${(h/100).toFixed(2)},%RH`);
                    }
                }
                const pr = dv.getInt16(p + OFF_P, true);
                if (pr !== NAN_S && wantAll) {
                    lines.push(`${iso},,,${(pr/10).toFixed(1)},hPa`);
                }
            }
            return lines;
        }

        /* Export chunked com chunk_size adaptativo + cancelamento + retry.
         *
         * Calibragem (tools/test_chunk_perf.sh):
         *   6h => 717ms  (5/5, 22s p/ 7d)
         *   12h => 2183ms (5/5)
         *   24h => 2635ms (5/5, 19s p/ 7d — sweet spot)
         *   48h => 6278ms (5/5, mas perto do deadline 10s)
         *
         * Estrategia:
         *  - Inicial: 24h (ou menos se heap_lb baixo)
         *  - Em falha: divide chunk pela metade (split) e re-tenta o mesmo cursor.
         *  - Apos N OK consecutivos: tenta aumentar de volta ate o teto.
         *  - Cancelavel via AbortController + flag.
         */
        const EXP_CHUNK_INITIAL = 86400;      /* 24h sweet spot */
        const EXP_CHUNK_MAX     = 86400;      /* nao passar de 24h (48h beira o limite) */
        const EXP_CHUNK_MIN     =  3600;      /* 1h minimo */
        const EXP_MAX_RETRIES   = 2;          /* por tamanho — split conta como retry mais agressivo */
        const EXP_RECOVERY_WIN  = 5;          /* OK consecutivos antes de tentar aumentar */
        let _expAbort = null;                 /* AbortController do fetch atual */
        let _expCancelled = false;

        function _fmtEta(ms) {
            if (ms <= 0 || !isFinite(ms)) return '...';
            const s = Math.ceil(ms / 1000);
            if (s < 60) return '~' + s + 's';
            if (s < 600) return '~' + (s/60|0) + 'm' + (s%60) + 's';
            return '~' + Math.round(s/60) + 'm';
        }
        function _showExpProgress(state) {
            let ov = document.getElementById('expOv');
            if (!ov) {
                ov = document.createElement('div');
                ov.id = 'expOv'; ov.className = 'exp-overlay';
                ov.innerHTML = '<div class="exp-overlay-box">'
                    + '<div class="exp-overlay-title">Exporting...</div>'
                    + '<div class="exp-overlay-bar"><div class="exp-overlay-fill" id="expF"></div></div>'
                    + '<div class="exp-overlay-stat"><span id="expP">0%</span><span id="expE" style="color:var(--acc)">...</span></div>'
                    + '<div class="exp-overlay-stat" style="margin-top:4px"><span class="ok" id="expK">0 OK</span><span id="expC" style="opacity:.7">--</span><span class="fail" id="expN">0</span></div>'
                    + '<button type="button" id="expX" style="margin-top:14px;padding:8px;background:transparent;color:var(--dang);border:1px solid var(--dang);border-radius:6px;cursor:pointer;font-weight:600;width:100%">Cancelar</button>'
                    + '</div>';
                document.body.appendChild(ov);
                document.getElementById('expX').onclick = () => {
                    _expCancelled = true;
                    if (_expAbort) try { _expAbort.abort(); } catch(e){}
                    const x = document.getElementById('expX');
                    x.textContent = '...'; x.disabled = true;
                };
            }
            document.getElementById('expF').style.width = state.pct + '%';
            document.getElementById('expP').textContent = state.pct + '%';
            document.getElementById('expK').textContent = state.ok + ' OK';
            document.getElementById('expN').textContent = state.fail + ' err';
            document.getElementById('expC').textContent = (state.chunkSecs/3600) + 'h';
            const e = document.getElementById('expE');
            if (state.pct >= 100) e.textContent = 'OK';
            else if (state.etaMs >= 0 && state.pct > 0) e.textContent = _fmtEta(state.etaMs);
            else e.textContent = '...';
        }
        function _hideExpProgress() {
            const ov = document.getElementById('expOv');
            if (ov) ov.remove();
        }

        /* Lock/unlock dos controles do grafico durante export — impede que
         * cliques acidentais (zoom/nav/sensores) recarreguem o grafico
         * enquanto o overlay esta visivel. */
        function _setGraphControlsEnabled(enabled) {
            const ids = ['btnPrev','btnNext','rangeSel','sensorMselBtn'];
            ids.forEach(id => {
                const el = document.getElementById(id);
                if (el) el.disabled = !enabled;
            });
            document.querySelectorAll('#sensorMselMenu input[type=checkbox]').forEach(c => c.disabled = !enabled);
            document.querySelectorAll('.cal-cell').forEach(c => c.style.pointerEvents = enabled ? '' : 'none');
        }

        async function exportHistoryCsv() {
            const btn = document.getElementById('btnExpHist');
            if (!_lastChartCutoff || !_lastChartEnd) {
                showToast(window.t('exp_no_chart', 'Load a chart range first.'), 'warn'); return;
            }
            const filterArr = _selectedSensors.slice();
            const from = _lastChartCutoff, to = _lastChartEnd;
            const totalSecs = to - from;
            const orig = btn.innerHTML;
            btn.disabled = true; btn.innerHTML = '⏳';
            _expCancelled = false;

            /* FEEDBACK IMEDIATO: overlay aparece ANTES de qualquer await.
             * Antes era depois do /api/status (600ms) — parecia travado. */
            _showExpProgress({ pct: 0, ok: 0, fail: 0, chunkSecs: EXP_CHUNK_INITIAL, etaMs: -1 });
            _setGraphControlsEnabled(false);

            /* Wrapper try/finally para garantir que controles SEMPRE voltam
             * a ser enabled mesmo em exceção inesperada (ex: decode lança,
             * fetch lança fora do retry, etc). */
            try {

            /* ── Export a partir dos arquivos .h5 ───────────────────────────
             *
             * Antes o aparelho decodificava o proprio historico e mandava
             * BinaryHistoryRecord de 70 bytes fixos: 33 canais, dos quais 27
             * eram o sentinela NAN numa bancada de 6 canais. Medido: 70,1 B
             * por registro na rede contra 5,63 B em flash, 12,5x, e um export
             * completo de 9,2 MB em quase seis minutos.
             *
             * Agora a pagina baixa os proprios arquivos por /download e
             * decodifica aqui. O aparelho faz uma leitura de arquivo e mais
             * nada, e o que chega e exatamente o arquivo de arquivo morto —
             * o mesmo que tools/history_v5.py --dump-csv le.
             *
             * A cadencia de amostragem NAO esta no arquivo: os simbolos de
             * tempo sao deltas contra o intervalo nominal, entao ele vem de
             * /api/status (campo hi, em minutos). Sem isso todo registro
             * depois do primeiro cairia na hora errada num aparelho que nao
             * amostre a cada minuto. */
            let nominalSecs = 60;
            const slotMeta = {};
            try {
                const sr = await fetchSafe('/api/status', {timeout: 8000, retries: 1});
                const sd = await sr.json();
                if (sd.sys && sd.sys.hi > 0) nominalSecs = sd.sys.hi * 60;
                for (const s of (sd.sensors || [])) {
                    slotMeta[s.slot] = { hwId: s.id, friendly: (s.name || '').replace(/"/g, "'") };
                }
            } catch (e) { /* segue com 60 s e sem nomes bonitos */ }

            /* Dias disponiveis, do mais novo para o mais antigo — se o usuario
             * cancelar no meio ele fica com o periodo recente, que e o que
             * costuma querer. */
            let days = [];
            try {
                const lr = await fetchSafe('/api/ls?dir=/history', {timeout: 10000, retries: 2});
                const lj = await lr.json();
                for (const e of (lj.entries || [])) {
                    const m = /^(\d{4})(\d{2})(\d{2})\.h5$/.exec(e.n);
                    if (!m) continue;
                    const d0 = Math.floor(new Date(+m[1], +m[2] - 1, +m[3]).getTime() / 1000);
                    const d1 = d0 + 86400;
                    if (d1 < from || d0 > to) continue;   /* fora da janela pedida */
                    days.push({ name: e.n, size: e.s || 0, t0: d0 });
                }
            } catch (e) {
                showToast(window.t('exp_empty', 'No data recovered.'), 'err');
                return;
            }
            days.sort((a, b) => b.t0 - a.t0);
            /* Zero dias NAO e mais saida antecipada: um aparelho na primeira
             * hora depois de um factory reset nao tem arquivo nenhum e tem a
             * hora aberta, que e justamente o que este export passou a
             * alcancar. O laco roda zero vezes e o teste de allLines vazio la
             * embaixo da o mesmo aviso quando de fato nao houver nada. */

            const filterSet = (filterArr && filterArr.length) ? new Set(filterArr.map(Number)) : null;
            /* Um array de linhas POR DIA. Os dias chegam do mais novo para o
             * mais antigo, mas as linhas dentro de um dia ja vem em ordem —
             * juntar tudo num array so e inverter no fim inverteria os dois
             * niveis e embaralharia cada dia. */
            const dayLines = [];
            const failedRanges = [];
            let okChunks = 0, failChunks = 0;
            const totalBytes = days.reduce((a, d) => a + d.size, 0) || 1;
            let doneBytes = 0;
            const ETA_WIN = 5;
            const etaSamples = [];

            _showExpProgress({ pct: 0, ok: 0, fail: 0, chunkSecs: 86400, etaMs: -1 });

            for (const day of days) {
                if (_expCancelled) break;
                const iterStart = performance.now();
                let success = false, lastErr = '';
                for (let attempt = 0; attempt < EXP_MAX_RETRIES && !success && !_expCancelled; attempt++) {
                    try {
                        /* O grafico ja baixou e guardou os bytes de cada dia
                         * fechado - o export decodifica do cache e so vai a
                         * rede pelo que faltar (ou pelo dia corrente, que
                         * cresce a cada selagem). */
                        let buf;
                        const rawc = _h5Raw.get(day.name);
                        if (rawc && rawc.size === day.size) {
                            buf = rawc.buf;
                        } else {
                            _expAbort = new AbortController();
                            const r = await fetch('/download?file=/history/' + day.name,
                                { credentials: 'include', signal: _expAbort.signal });
                            if (!r.ok) throw new Error('HTTP ' + r.status);
                            buf = await r.arrayBuffer();
                            _h5Raw.set(day.name, { size: buf.byteLength, buf });
                        }
                        const dec = h5Decode(new Uint8Array(buf), nominalSecs);
                        if (dec.rejected) {
                            /* Bloco corrompido nao invalida o dia: o que
                             * sobreviveu vai para o CSV e o resto e contado. */
                            failedRanges.push({ from: day.t0, to: day.t0 + 86400,
                                                err: dec.rejected + ' chunk(s)' });
                        }
                        dayLines.push(h5ToCsvLines(dec, slotMeta, filterSet, from, to, _isoLocal));
                        success = true;
                    } catch (e) {
                        lastErr = String(e.message || e);
                        if (e.name === 'AbortError') break;
                        if (attempt < EXP_MAX_RETRIES - 1) {
                            await new Promise(r => setTimeout(r, 300 * (attempt + 1)));
                        }
                    }
                }
                _expAbort = null;
                if (_expCancelled) break;

                if (success) okChunks++;
                else { failChunks++; failedRanges.push({ from: day.t0, to: day.t0 + 86400, err: lastErr }); }

                doneBytes += day.size;
                const ratio = day.size / totalBytes;
                if (ratio > 0) {
                    etaSamples.push({ ms: performance.now() - iterStart + 50, ratio });
                    if (etaSamples.length > ETA_WIN) etaSamples.shift();
                }
                let etaMs = -1;
                if (etaSamples.length >= 2) {
                    const sumMs = etaSamples.reduce((a, x) => a + x.ms, 0);
                    const sumR = etaSamples.reduce((a, x) => a + x.ratio, 0);
                    if (sumR > 0) etaMs = (sumMs / sumR) * Math.max(0, 1 - doneBytes / totalBytes);
                }
                _showExpProgress({ pct: Math.min(99, Math.round((doneBytes / totalBytes) * 100)),
                                   ok: okChunks, fail: failChunks, chunkSecs: 86400, etaMs });
                if (!_expCancelled) await new Promise(r => setTimeout(r, 20));
            }

            /* ── A hora ainda aberta ────────────────────────────────────────
             * Um bloco V5 so chega ao arquivo do dia quando sela, uma vez por
             * hora — entao os ultimos minutos nunca estiveram em .h5 nenhum e
             * o CSV parava na ultima selagem, por mais recente que fosse a
             * janela pedida. O aparelho serve esse bloco em /api/history/open
             * no MESMO formato dos arquivos, entao aqui e o mesmo h5Decode e
             * o mesmo h5ToCsvLines — nao ha segundo decodificador.
             *
             * Buscado DEPOIS dos arquivos, de proposito. Se uma selagem cair
             * no meio do export, o pior caso e uma lacuna (registros que
             * foram para o arquivo do dia depois de o termos baixado), nunca
             * uma linha duplicada — e linha repetida num CSV e pior que linha
             * faltando. Uma falha aqui nao derruba o export: o que ja foi
             * decodificado vale. */
            if (!_expCancelled) {
                try {
                    const r = await fetchSafe('/api/history/open',
                                              { timeout: 10000, retries: 1 });
                    /* 204 = nada aberto (o minuto seguinte a uma selagem). */
                    if (r.ok && r.status !== 204) {
                        const buf = await r.arrayBuffer();
                        if (buf.byteLength > 0) {
                            const dec = h5Decode(new Uint8Array(buf), nominalSecs);
                            const ln = h5ToCsvLines(dec, slotMeta, filterSet, from, to, _isoLocal);
                            /* unshift, nao push: dayLines esta do mais novo
                             * para o mais antigo e e invertido logo abaixo. */
                            if (ln.length) dayLines.unshift(ln);
                        }
                    }
                } catch (e) { /* a cauda e um bonus, nao um requisito */ }
            }

            /* Os dias vieram do mais novo para o mais antigo; o CSV sai em
             * ordem cronologica, com cada dia intacto. */
            const allLines = [];
            for (let i = dayLines.length - 1; i >= 0; i--) {
                for (const ln of dayLines[i]) allLines.push(ln);
            }

            if (allLines.length === 0) {
                if (_expCancelled) showToast(window.t('exp_cancelled', 'Cancelled.'), 'warn');
                else showToast(window.t('exp_empty', 'No data recovered.'), 'err');
                return;
            }

            const csv = '﻿' + 'timestamp_iso,sensor_id,sensor_name,value,unit\n' + allLines.join('\n') + '\n';
            const blob = new Blob([csv], {type:'text/csv;charset=utf-8'});
            const a = document.createElement('a');
            a.href = URL.createObjectURL(blob);
            const sufx = (filterArr.length === 1) ? ('_s' + filterArr[0]) : ('_n' + filterArr.length);
            const dt = new Date(from * 1000);
            const stamp = dt.getFullYear() + String(dt.getMonth()+1).padStart(2,'0') + String(dt.getDate()).padStart(2,'0');
            const cancelMark = _expCancelled ? '_partial' : '';
            a.download = 'simut_history' + sufx + cancelMark + '_' + stamp + '.csv';
            document.body.appendChild(a); a.click();
            setTimeout(function(){ URL.revokeObjectURL(a.href); a.remove(); }, 100);

            const tag = _expCancelled ? '⚠ cancelado: ' : (failChunks > 0 ? '⚠ ' : '');
            const stat = (_expCancelled || failChunks > 0) ? 'warn' : 'ok';
            const summ = tag + allLines.length + ' linhas' + (failChunks > 0 ? ' (' + failChunks + ' chunks falhos)' : '');
            showToast(summ, stat);
            if (failChunks > 0) console.warn('Chunks falhos:', failedRanges);

            } finally {
                /* Garante que controles voltam mesmo em exceção fatal */
                _hideExpProgress();
                _setGraphControlsEnabled(true);
                btn.disabled = false; btn.innerHTML = orig;
            }
        }

        /* Exporta o que esta visivel na lista de eventos (parsedLogRows
         * com filtros do filterLogs replicados). Sem fetch novo. */
        function exportLogsCsv() {
            if (!parsedLogRows || parsedLogRows.length === 0) {
                showToast(window.t('exp_logs_empty', "Click 'Load' first."), 'warn'); return;
            }
            const showInf = document.getElementById('chkInf').checked;
            const showWrn = document.getElementById('chkWrn').checked;
            const showErr = document.getElementById('chkErr').checked;
            const search = document.getElementById('logSearch').value.toLowerCase();
            const lines = [];
            for (const r of parsedLogRows) {
                let lvlOk = false;
                if (r.lvl <= 1 && showInf) lvlOk = true;
                if (r.lvl === 2 && showWrn) lvlOk = true;
                if (r.lvl >= 3 && showErr) lvlOk = true;
                if (!lvlOk) continue;
                if (search.length > 0) {
                    const hay = (r.dateStr + ' ' + r.upStr + ' ' + r.lvlLabel + ' ' + r.tag + ' ' + r.desc).toLowerCase();
                    if (hay.indexOf(search) < 0) continue;
                }
                const iso = (r.epoch && r.epoch > 0) ? _isoLocal(r.epoch) : r.dateStr;
                const msgRaw = (typeof evtName === 'function') ? evtName(r.code) : ('Event #' + r.code);
                const msgEsc = msgRaw.replace(/"/g, '""');
                lines.push(iso + ',' + r.lvlLabel + ',' + r.tag + ',' + r.code + ',"' + msgEsc + '",' + r.ctx + ',' + r.upHr);
            }
            if (lines.length === 0) { showToast(window.t('exp_empty','No data in this range.'), 'warn'); return; }
            const csv = '﻿' + 'timestamp_iso,level,module,code,message,context,uptime_sec\n' + lines.join('\n') + '\n';
            const blob = new Blob([csv], {type:'text/csv;charset=utf-8'});
            const a = document.createElement('a');
            a.href = URL.createObjectURL(blob);
            const stamp = new Date().toISOString().slice(0,10).replace(/-/g,'');
            a.download = 'simut_logs_' + stamp + '.csv';
            document.body.appendChild(a); a.click();
            setTimeout(function(){ URL.revokeObjectURL(a.href); a.remove(); }, 100);
            showToast(lines.length + ' ' + window.t('exp_rows','rows'), 'ok');
        }

        /* ===================================================================== */

        /* Qualquer erro JS vira aviso visivel — nunca mais tela em branco muda. */
        window.addEventListener('error', e => { try { showOverlay('Erro JS: ' + e.message); } catch(_) {} });
        document.addEventListener('DOMContentLoaded', () => { loadAvailableDays(); populateSensorMsel().then(() => loadGraphRange(2)); });
    </script>
</body>
</html>
)raw";


static const char CFG_PAGE[] PROGMEM = R"raw(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate">
    <title>SIMUT - Config</title>
    <script src="/lang.js"></script>
    <link rel="stylesheet" href="/style.css">
    <style>


        /* Config Styles */
        h3 { color: var(--txt); border-bottom: 1px solid var(--border); padding-bottom: 10px; margin-top: 30px; font-size: 1.1rem; }
        .grp { background: rgba(255,255,255,0.02); padding: 20px; border-radius: 8px; margin-bottom: 15px; border: 1px solid var(--border); }
        label { display: block; color: var(--sub); margin-bottom: 6px; font-size: 0.9rem; font-weight: 600; }
        .card input[type=text], .card input[type=password], .card input[type=number], .card input[type=date], .card input[type=time], .card select { width: 100%; padding: 12px; background: var(--bg); border: 1px solid var(--border); color: var(--txt); border-radius: 6px; box-sizing: border-box; margin-bottom: 15px; font-size: 1rem; transition: 0.2s; }
        .card input:focus, .card select:focus { border-color: var(--acc); outline: none; }
        .card button[type=submit] { width: 100%; padding: 14px; background: var(--acc); color: black; border: none; font-weight: bold; border-radius: 6px; cursor: pointer; font-size: 1rem; margin-top: 20px; transition: 0.2s; }
        .card button[type=submit]:hover { opacity: 0.9; transform: translateY(-1px); }
        .chk { display: flex; align-items: center; gap: 10px; margin-bottom: 15px; }
        .chk input[type=checkbox] { width: 18px; height: 18px; accent-color: var(--acc); cursor: pointer; }
        .cfg-tg { display: flex; align-items: center; gap: 12px; padding: 8px 0; cursor: pointer; }
        .row { display: flex; gap: 20px; }
        .col { flex: 1; }
        @media(max-width: 600px) { .row { flex-direction: column; gap: 0; } }
        .builder-box { background: var(--bg); border: 1px solid var(--border); border-radius: 8px; padding: 15px; margin-top: 15px; }
        /* Sensor slot editor. Declared once here instead of as inline style
           strings inside the JS that builds the rows, and deliberately reusing
           the conventions already set elsewhere in the interface: the table
           rule from /users, the pill and the field label from /alarms. Sizes
           are all rem, matching the rest of the page — the section had grown
           its own scale (0.78em/0.8em/0.85em/0.9em) and read as a bolt-on. */
        /* Sem isto a tabela de 6 colunas transborda para o documento e arrasta a
           pagina inteira de lado no celular, nao so a tabela. */
        /* .tbl-scroll contem o transbordo aqui em vez de deixar a tabela de 6
           colunas arrastar a pagina inteira de lado. Sem overflow-wrap nos td:
           ele derruba a largura minima da celula para ~1 caractere, a tabela
           "cabe" nos 100% e empilha o texto em coluna. Preferimos o deslize. */
        .tbl-scroll { overflow-x: auto; }
        /* Sangra ate as bordas do .grp no celular: encaixada para dentro, a
           tabela ficava num canal de 260px e o corte no meio do card nao lia
           como "da para arrastar". No painel ela ocupa o card inteiro e por isso
           parece deslizavel — aqui fica igual. */
        @media(max-width: 640px) {
            .tbl-scroll { margin-left: -20px; margin-right: -20px; }
            .tbl-scroll > table { min-width: max-content; }
        }
        #sens_tbl { width: 100%; border-collapse: collapse; }
        #sens_tbl th, #sens_tbl td { padding: 12px 14px; text-align: left; border-bottom: 1px solid var(--border); }
        #sens_tbl th { color: var(--sub); font-size: 0.85rem; text-transform: uppercase; font-weight: 600; }
        .sxm { font-family: monospace; }
        .sxb { background: var(--track); color: var(--txt); border: none; padding: 6px 12px; border-radius: 4px; cursor: pointer; font-size: 0.85rem; font-weight: 600; transition: 0.2s; }
        .sxb:hover { background: #52525b; }
        .sxb-dang { background: transparent; border: 1px solid var(--dang); color: var(--dang); padding: 5px 12px; }
        .sxb-dang:hover { background: var(--dang); color: #fff; }
        .sxb-on { background: var(--acc); color: #000; }
        .sxb-on:hover { background: var(--acc); }
        /* Mini grafico de correcao por canal: a linha tracejada e o padrao do
           sensor (delta zero), a curva e a correcao com suas ancoras. Cores em
           classes, nunca em atributos fill/stroke — var() nao resolve la. */
        .spk { display: block; width: 100%; height: 74px; background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.08); border-radius: 6px; margin: 6px 0 2px; }
        .spk .l0 { stroke: #71717a; stroke-width: 1; stroke-dasharray: 4 4; }
        .spk .lc { stroke: var(--acc); stroke-width: 2; fill: none; }
        .spk .pa { fill: var(--acc); stroke: var(--card); stroke-width: 1.5; }
        .spk .hz { fill: rgba(255,255,255,0.05); }
        .spk .tk { stroke: var(--warn); stroke-width: 1.2; }
        /* Grade em vez de flex-wrap: com flex cada pilula tinha a largura do seu
           texto (GP1 vs GP12 2) e as linhas saiam desencontradas. Colunas iguais
           alinham tudo — 4 colunas a 360px, mais no desktop. */
        .sxg-map { display: grid; grid-template-columns: repeat(auto-fill, minmax(58px, 1fr)); gap: 4px; margin-bottom: 14px; }
        .sxg { font-size: 0.8rem; font-family: monospace; color: var(--sub); background: rgba(255,255,255,0.05); padding: 4px 6px; border-radius: 12px; text-align: center; }
        .sxg.on { background: var(--acc); color: #000; }
        .sxg i { font-style: normal; font-weight: 700; margin-left: 5px; opacity: 0.75; }
        .sxh { display: block; color: var(--sub); margin-bottom: 4px; font-size: 0.82rem; font-weight: 600; }
        /* Prose note, as opposed to .sxh which labels a field. Same size, normal
           weight — a paragraph set in label weight reads as a heading. */
        .sxn { display: block; color: var(--sub); margin-top: 4px; font-size: 0.82rem; font-weight: 400; line-height: 1.45; }
        label.sxsec { margin-top: 18px; }
        .sx-slot { font-weight: 700; font-size: 1.05rem; color: var(--acc); }
        .sx-warn { display: none; margin-top: 12px; padding: 10px 14px; background: rgba(239,68,68,0.12); border-left: 3px solid var(--dang); border-radius: 3px; font-size: 0.85rem; }
        /* One calibration point: raw + reference inputs, capture and remove.
           A grid, not flex — equal columns keep the rows of a 5-point table
           aligned regardless of how wide each typed number is. */
        .sxpt { display: grid; grid-template-columns: 1fr 1fr auto auto; gap: 6px; margin-bottom: 6px; align-items: center; }
        .sxpt .sxb { padding: 6px 10px; }
        /* Slot dialog. Mirrors .exp-overlay from the history page, plus a
           scroll cap: the editor is taller than the export progress box it is
           modelled on. It stays inside .card on purpose — position:fixed takes
           it out of the layout anyway, and living there keeps the .card input
           and .card select rules applying to its fields. */
        .sx-ov { position: fixed; inset: 0; background: rgba(12,15,19,0.85); z-index: 9000; display: flex; align-items: center; justify-content: center; padding: 20px; }
        .sx-ov-box { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 24px; min-width: 320px; width: 640px; max-width: 100%; max-height: 90vh; overflow-y: auto; box-shadow: 0 8px 24px rgba(0,0,0,0.6); }
        #sens_tbl tbody tr { cursor: pointer; transition: background 0.15s; }
        #sens_tbl tbody tr:hover { background: rgba(255,255,255,0.04); }
        .sx-empty { color: var(--sub); text-align: center; padding: 28px 0; font-size: 0.9rem; }
        .card #sens_ed input, .card #sens_ed select { margin-bottom: 0; }
        #preview { background: #18181b; color: #a1a1aa; padding: 15px; border-radius: 8px; font-family: monospace; font-size: 0.85rem; overflow-x: auto; border: 1px dashed #3f3f46; white-space: pre-wrap; word-break: break-all; }
        .highlight { color: var(--ok); font-weight: bold; }
    </style>
    <script>
        /* window.t/applyLang/setLang/showToast/fetchSafe vem de /lang.js */

        /* U24 Phase D: Pending + commitAll centralizados em /lang.js. */
    </script>
</head>
<body>
    <script>installTopbar('nav_cfg', 'System Config')</script>

    <div class="container">
        <div class="card">
            <h2 class="page-title" data-i18n="cfg_title">System Settings</h2>
            <!-- Shown when /api/config does not resolve. Without it a failed
                 load left the form blank AND inert, with nothing on screen to
                 say why: loadConfig( ) wires its input listeners on its last
                 line, so any earlier failure silently produced a page that
                 looked editable and discarded every keystroke. -->
            <div id="cfg_load_err" style="display:none;margin-bottom:14px;padding:10px 14px;background:rgba(239,68,68,0.12);border-left:3px solid #ef4444;border-radius:3px;font-size:0.92em">
                <span id="cfg_load_err_msg" data-i18n="cfg_load_fail">Could not load the current settings. The fields are disabled to avoid saving blank values over your configuration.</span>
                <button type="button" id="cfg_retry" onclick="loadConfig()" style="margin-left:10px;padding:4px 12px;background:var(--acc);color:#000;border:none;border-radius:4px;cursor:pointer;font-weight:bold" data-i18n="cfg_retry">Retry</button>
            </div>
            <form id="sysForm" onsubmit="event.preventDefault()">
                <h3 data-i18n="cfg_gen" style="margin-top:0;">General Identity</h3>
                <div class="grp">
                    <div class="row">
                        <div class="col">
                            <label data-i18n="cfg_dev">Device Name</label>
                            <input type="text" id="name" name="name" maxlength="31" required>
                        </div>
                        <div class="col">
                            <label data-i18n="cfg_tz">Timezone Offset (Hours)</label>
                            <input type="number" id="tz" name="tz" min="-12" max="14" step="1" required>
                        </div>
                    </div>
                    <label class="cfg-tg">
                        <span class="toggle"><input type="checkbox" id="log" name="log" value="1"><span class="slider"></span></span>
                        <span data-i18n="cfg_log">Enable Local Logging</span>
                    </label>
                </div>

                <h3 data-i18n="cfg_datetime">Date &amp; Time</h3>
                <div class="grp">
                    <label class="cfg-tg">
                        <span class="toggle"><input type="checkbox" id="ntp_enabled" name="ntp_enabled" value="1" onchange="toggleManualTime()"><span class="slider"></span></span>
                        <span data-i18n="cfg_ntp_auto">Synchronize automatically via NTP</span>
                    </label>
                    <div id="manual_time_fields">
                        <div class="row" style="margin-top:12px">
                            <div class="col">
                                <label data-i18n="cfg_date">Date</label>
                                <input type="date" id="man_date" min="2026-01-01" style="color-scheme:light dark">
                            </div>
                            <div class="col">
                                <label data-i18n="cfg_time">Time</label>
                                <input type="time" id="man_time" step="1" style="color-scheme:light dark">
                            </div>
                        </div>
                        <button type="button" class="b-pri" onclick="applyManualTime()" style="margin-top:12px" data-i18n="cfg_apply_now">Apply Now</button>
                        <div class="c-sub" style="margin-top:8px;font-size:0.8em;color:var(--sub)" data-i18n="cfg_manual_hint">Uses device timezone (offset above). Save &amp; Reboot to persist the NTP toggle.</div>
                        <div id="time_result" style="margin-top:8px;font-size:0.9em"></div>
                    </div>
                </div>

                <h3 data-i18n="cfg_hw">Hardware & Sampling</h3>
                <div class="grp">
                    <div class="row">
                        <div class="col">
                            <label data-i18n="cfg_res">DS18B20 Resolution</label>
                            <select id="res" name="res">
                                <option value="9" data-i18n="cfg_r9">9-bit (Fast)</option>
                                <option value="10">10-bit</option>
                                <option value="11">11-bit</option>
                                <option value="12" data-i18n="cfg_r12">12-bit (Precise)</option>
                            </select>
                        </div>
                        <div class="col">
                            <label data-i18n="cfg_sint">Sample Interval (ms)</label>
                            <input type="number" id="s_int" name="s_int" min="1000" max="60000" required>
                        </div>
                    </div>
                    <div class="row" style="margin-top:15px;">
                        <div class="col">
                            <label data-i18n="cfg_hint">History Recording Interval (min)</label>
                            <input type="number" id="h_int" name="h_int" min="1" max="1440" required>
                            <div class="c-sub" style="margin-top:4px;font-size:0.8em;color:var(--sub)" data-i18n="cfg_hint_hint">1 to 1440 min (24h). Default: 1.</div>
                        </div>
                    </div>
                </div>

                <h3 data-i18n="sens_title">Sensors &amp; GPIO</h3>
                <div class="grp">
                    <div id="sens_err" class="sx-warn" style="margin-top:0;margin-bottom:10px"></div>
                    <div class="sxn" style="margin-bottom:12px;margin-top:0" data-i18n="sens_hint">GP0-GP15 are available to sensors; GP16 and above belong to the display, touch and buzzer.</div>
                    <div id="sens_map" class="sxg-map"></div>
                    <div class="tbl-scroll">
                    <table id="sens_tbl">
                        <thead><tr>
                            <th data-i18n="sens_slot">Slot</th>
                            <th data-i18n="sens_type">Type</th>
                            <th data-i18n="sens_gpios">GPIOs</th>
                            <th data-i18n="sens_hwid">Hardware ID</th>
                            <th data-i18n="sens_name">Name</th>
                            <th></th>
                        </tr></thead>
                        <tbody id="sens_tb"></tbody>
                    </table>
                    </div>
                    <button type="button" class="sxb" id="sens_add" onclick="sensAdd()" style="margin-top:14px" data-i18n="sens_add">+ Add sensor slot</button>
                    <button type="button" class="sxb" id="sens_scan" onclick="sensScan()" style="margin-top:14px;margin-left:8px" data-i18n="sens_scan">Scan for probes</button>
                    <div class="sxn" id="sens_scan_out" style="margin-top:10px"></div>
                    <div class="sxn" id="sens_full" style="display:none" data-i18n="sens_no_free">All 16 slots are in use. Free one to add another.</div>
                    <div id="sens_ov" class="sx-ov" style="display:none" onclick="sensBackdrop(event)">
                        <div id="sens_ed" class="sx-ov-box"></div>
                    </div>
                </div>

                <h3 data-i18n="cfg_tel">Telemetry Engine</h3>
                <div class="grp">
                    <div class="row" style="margin-bottom:15px;">
                        <div class="col">
                            <label data-i18n="cfg_transport">Transport Protocol</label>
                            <select id="t_transport" name="t_transport" onchange="toggleTransport()">
                                <option value="0" data-i18n="cfg_tr_http">HTTP / HTTPS (POST)</option>
                                <option value="1" data-i18n="cfg_tr_mqtt">MQTT / MQTTS</option>
                            </select>
                        </div>
                    </div>

                    <!-- Campos compartilhados: Server, Port e TLS (usados por HTTP e MQTT) -->
                    <div class="row">
                        <div class="col" style="flex:2;">
                            <label data-i18n="cfg_srv">Server Domain / IP</label>
                            <input type="text" id="t_srv" name="t_srv" maxlength="63">
                        </div>
                        <div class="col">
                            <label data-i18n="cfg_port">Port</label>
                            <input type="number" id="t_port" name="t_port" min="1" max="65535">
                        </div>
                    </div>
                    <label class="cfg-tg" style="margin-top:5px;">
                        <span class="toggle"><input type="checkbox" id="t_sec" name="t_sec" value="1" onchange="updateTlsWarn()"><span class="slider"></span></span>
                        <span id="t_sec_lbl" data-i18n="cfg_sec">Use TLS / SSL</span>
                    </label>
                    <!-- M-8: TLS on without /cert.pem = encrypted but NOT
                         authenticated (setInsecure). Seal shown from t_sec+t_cert. -->
                    <div id="tls_noverify_warn" style="display:none;margin:6px 0 0;padding:8px 10px;border-radius:6px;background:rgba(245,158,11,.12);border:1px solid var(--warn);color:var(--warn);font-size:.82rem">
                        <span data-i18n="cfg_tls_noverify">⚠ TLS without certificate validation — the connection is encrypted but not authenticated (MITM possible). Upload /cert.pem via Files to validate the server.</span>
                    </div>

                    <!-- Campos exclusivos do transporte HTTP -->
                    <div id="http_fields" style="border-top:1px dashed #3f3f46; padding-top:15px; margin-top:5px;">
                        <div class="row">
                            <div class="col">
                                <label data-i18n="cfg_path">Endpoint Path (URL)</label>
                                <input type="text" id="t_path" name="t_path" maxlength="31" placeholder="/api/v1/telemetry">
                            </div>
                            <div class="col">
                                <label data-i18n="cfg_key">API Key / Token</label>
                                <input type="password" id="t_key" name="t_key" maxlength="63">
                            </div>
                        </div>
                    </div>

                    <!-- Campos exclusivos do transporte MQTT -->
                    <div id="mqtt_fields" style="display:none; border-top:1px dashed #3f3f46; padding-top:15px; margin-top:5px;">
                        <div class="row">
                            <div class="col">
                                <label data-i18n="cfg_mq_topic">MQTT Topic</label>
                                <input type="text" id="m_topic" name="m_topic" maxlength="63" placeholder="telemetry/data">
                            </div>
                            <div class="col">
                                <label data-i18n="cfg_mq_cid">Client ID</label>
                                <input type="text" id="m_cid" name="m_cid" maxlength="23" placeholder="SIMUT_123">
                            </div>
                        </div>
                        <div class="row">
                            <div class="col">
                                <label data-i18n="cfg_mq_user">MQTT User</label>
                                <input type="text" id="m_user" name="m_user" maxlength="31">
                            </div>
                            <div class="col">
                                <label data-i18n="cfg_mq_pass">MQTT Password</label>
                                <input type="password" id="m_pass" name="m_pass" maxlength="31" placeholder="Leave empty to keep">
                            </div>
                        </div>
                        <div class="row">
                            <div class="col">
                                <label data-i18n="cfg_mq_qos">QoS Level</label>
                                <select id="m_qos" name="m_qos">
                                    <option value="0" data-i18n="cfg_mq_q0">0 - At Most Once</option>
                                    <option value="1" data-i18n="cfg_mq_q1">1 - At Least Once</option>
                                    <option value="2" data-i18n="cfg_mq_q2">2 - Exactly Once</option>
                                </select>
                            </div>
                            <div class="col">
                                <label data-i18n="cfg_mq_ka">Keep-Alive (s)</label>
                                <input type="number" id="m_ka" name="m_ka" min="10" max="300">
                            </div>
                        </div>
                        <label class="chk">
                            <input type="checkbox" id="m_retain" name="m_retain" value="1">
                            <span data-i18n="cfg_mq_retain">Retain Message</span>
                        </label>
                    </div>

                    <div class="row" style="margin-top: 15px; border-top:1px solid #3f3f46; padding-top:15px;">
                        <div class="col">
                            <label data-i18n="cfg_tint">Upload Interval (ms)</label>
                            <input type="number" id="t_int" name="t_int" min="0" max="86400000">
                            <div class="c-sub" style="margin-top:4px;font-size:0.8em;color:var(--sub)" data-i18n="cfg_tint_hint">Set 0 to disable telemetry. Minimum recommended: 10000 (10s).</div>
                        </div>
                        <div class="col">
                            <label data-i18n="cfg_bat">Batch Limit</label>
                            <input type="number" id="t_bat" name="t_bat" min="1" max="50">
                        </div>
                    </div>
                    <div id="tel_disabled_warn" style="display:none;margin-top:10px;padding:8px 12px;background:rgba(255,180,0,0.12);border-left:3px solid #f59e0b;border-radius:3px;font-size:0.9em" data-i18n="cfg_tel_disabled">⚠ Telemetry disabled (Upload Interval = 0). Set a value to enable.</div>
                    <!-- Act on the running device, not on the staged form: both
                         answer "is the endpoint reachable right now", which is
                         the question you ask while editing these fields. -->
                    <div style="margin-top:16px;border-top:1px solid #3f3f46;padding-top:14px">
                      <div style="display:flex;gap:8px;flex-wrap:wrap">
                        <button type="button" class="sxb" id="tel_sync_btn" onclick="telSync()" data-i18n="tel_sync">Send now</button>
                        <button type="button" class="sxb sxb-dang" id="tel_reset_btn" onclick="telReset()" data-i18n="tel_reset">Reset send cursor</button>
                      </div>
                      <div class="c-sub" style="margin-top:8px;font-size:0.8em;color:var(--sub)" data-i18n="tel_hint">Send now flushes whatever is pending without waiting for the interval. Reset send cursor makes the device re-send up to 30 days back — use it after a long server outage.</div>
                    </div>
                </div>

                <h3 data-i18n="cfg_vis">Payload Builder</h3>
                <div class="grp">
                    <label data-i18n="cfg_fmt">Format</label>
                    <select id="t_mode" name="t_mode" onchange="toggleBuilder()">
                        <option value="0" data-i18n="cfg_f0">JSON Array (Standard)</option>
                        <option value="1" data-i18n="cfg_f1">CSV Raw (Standard)</option>
                        <option value="2" data-i18n="cfg_f2">Dynamic Builder (Advanced)</option>
                    </select>

                    <div id="custom_tools" class="builder-box">
                        <div style="font-size:0.85rem; color:var(--sub); margin-bottom:15px;" data-i18n="cfg_leg">Payload Tag Reference:</div>
                        <div class="row tag-ref" style="margin-bottom:15px; font-size:0.8rem; padding:10px; border-radius:6px; border:1px solid var(--border);">
                            <div class="col">
                                <b style="color:var(--txt);" data-i18n="cfg_leg1">Global Tags:</b><br>
                                <span class="highlight">{DEV}</span> - Device Name<br>
                                <span class="highlight">{MAC}</span> - MAC Address<br>
                                <span class="highlight">{DATA}</span> - Where the records go
                            </div>
                            <div class="col">
                                <b style="color:var(--txt);" data-i18n="cfg_leg2">Data Row Tags:</b><br>
                                <span class="highlight">{TS}</span> - Unix Epoch<br>
                                <span class="highlight">{DHT_ID}</span> - Board Serial<br>
                                <span class="highlight">{t0}</span> to <span class="highlight">{t15}</span> - Temperature<br>
                                <span class="highlight">{u0}</span> to <span class="highlight">{u15}</span> - Humidity<br>
                                <span class="highlight">{p0}</span> to <span class="highlight">{p15}</span> - Pressure<br>
                            </div>
                        </div>
                        <div style="font-size:0.8rem; color:var(--txt); margin-bottom:15px; border-left:3px solid var(--acc); padding-left:10px;">
                            <b data-i18n="cfg_leg3">Smart Keys:</b> <span data-i18n="cfg_leg4">Use exact formats to inject ID and omit off sensors:</span><br>
                            <span class="highlight">"t0_ID":{t0}</span> &rarr; <span class="highlight">"t28FF31...":24.5</span><br>
                            <span class="highlight">"u0_ID":{u0}</span> &rarr; <span class="highlight">"u28FF31...":55.2</span>
                        </div>

                        <label data-i18n="cfg_tpl1">1. Global Template (The Envelope)</label>
                        <input type="text" id="t_glob" name="t_glob" maxlength="255" placeholder='{"device":"{DEV}", "payload":[{DATA}]}' oninput="renderPreview()">

                        <label data-i18n="cfg_tpl2">2. Row Template (Single Reading)</label>
                        <input type="text" id="t_line" name="t_line" maxlength="511" placeholder='{"time":{TS}, "t0_ID":{t0}, "u0_ID":{u0}, "t1_ID":{t1}}' oninput="renderPreview()">

                        <label data-i18n="cfg_tpl3">3. Separator</label>
                        <input type="text" id="t_sep" name="t_sep" maxlength="7" placeholder="," oninput="renderPreview()">
                    </div>

                    <div style="margin-top:15px;">
                        <label data-i18n="cfg_prev">Live Preview:</label>
                        <div id="preview"></div>
                    </div>
                </div>

                <!-- U24: save button removido. Use "Salvar e Reiniciar" no topbar. -->
            </form>

            <div style="margin-top:24px;padding-top:16px;border-top:1px solid var(--brd)">
                <h3 data-i18n="cfg_touch_title">Touch Calibration</h3>
                <button type="button" onclick="resetTouchCal()" style="background:var(--dang);color:#fff;border:none;padding:10px 20px;border-radius:8px;cursor:pointer;font-size:0.95em" data-i18n="cfg_touch_reset">Reset Touch Calibration</button>
                <div class="c-sub" style="margin-top:6px;font-size:0.8em;color:var(--sub)" data-i18n="cfg_touch_hint">Clears the stored calibration and starts the wizard on the display — follow the on-screen steps there.</div>
            </div>
        </div>
    </div>

    <script>
        function toggleTransport() { let tr = document.getElementById('t_transport').value; document.getElementById('http_fields').style.display = (tr == '0') ? 'block' : 'none'; document.getElementById('mqtt_fields').style.display = (tr == '1') ? 'block' : 'none'; let secSpan = document.getElementById('t_sec_lbl'); if (secSpan) secSpan.textContent = (tr == '1') ? window.t('cfg_sec_mqtt', 'Use MQTTS (TLS)') : window.t('cfg_sec', 'Use HTTPS (SSL)'); }
        /* M-8: show the "no cert validation" seal when TLS is on but no cert was
           loaded at boot (window.__tlsCert, from /api/config t_cert). Advisory —
           the firmware still refuses nothing, it just makes setInsecure visible. */
        function updateTlsWarn() { var w = document.getElementById('tls_noverify_warn'); if (!w) return; var on = document.getElementById('t_sec').checked; w.style.display = (on && !window.__tlsCert) ? 'block' : 'none'; }
        function toggleBuilder() { let mode = document.getElementById('t_mode').value; document.getElementById('custom_tools').style.display = (mode == '2') ? 'block' : 'none'; renderPreview(); }

        /* Device metadata populated by loadConfig (real serial + per-slot hwid/active).
         * Defaults used until /api/config resolves. */
        let _devSerial = 'RP2040_A1B2';
        let _devSensors = Array.from({length:10}, (_,i) => ({
            hwid: 'STM' + String(i+1).padStart(4,'0'),
            active: true, hasHum: false, hasPress: false
        }));

        /* Demo scenario: real serial + real per-slot hwid/active state, 2-record batch.
         * Value formatting mirrors firmware (%.2f for temperature, %.1f for
         * humidity and pressure). */
        function _previewDemoBatch() {
            /* hum is needed for {u..} to render as a number instead of null —
             * without it the new humidity tokens would preview as missing even
             * on a sensor that reports humidity. */
            const mk = (tBase) => _devSensors.map((s, i) => ({
                hwid: s.hwid,
                val: (20 + i + tBase).toFixed(2),
                hum: s.hasHum ? (55 + i + tBase).toFixed(1) : null,
                hasPress: !!s.hasPress,
                active: s.active
            }));
            /* {DHT_ID} is the one device-level key left: it resolves to the
               board serial. The {tAMB}/{uAMB}/{pAMB} trio went with the ambient
               slot — every value now belongs to the slot that measured it. */
            return [
                { ts: 1700000000, serial: _devSerial, press: '1013.2', slots: mk(0.1) },
                { ts: 1700000005, serial: _devSerial, press: '1013.1', slots: mk(0.2) }
            ];
        }

        /* Mirror of firmware formatLineCustomBuf: single-pass token matching
         * with compound look-back `"<k>_ID":{<k>}` and `"<k>":{<k>}`. */
        function _previewCustomLine(tpl, rec) {
            let out = '', ti = 0;
            while (ti < tpl.length) {
                const c = tpl[ti];
                if (c !== '{') { out += c; ti++; continue; }
                let val = null, hwid = null, compKey = '', tc = 0;
                if (tpl.substr(ti, 4) === '{TS}') { val = String(rec.ts); tc = 4; }
                else if (tpl.substr(ti, 8) === '{DHT_ID}') { val = rec.serial; tc = 8; }
                else {
                    /* {t0}..{t15} and {u0}..{u15}. The preview used to know only
                     * single-digit {t..}, so {u1} and {t12} fell through and were
                     * echoed literally — the template looked broken here while the
                     * firmware resolved it fine. One matcher for both channels
                     * keeps the two sides from drifting apart again. */
                    const ch = tpl[ti+1];
                    if (ch === 't' || ch === 'u' || ch === 'p') {
                        let digits = 0;
                        if (tpl[ti+2] >= '0' && tpl[ti+2] <= '9') {
                            if (tpl[ti+3] === '}') digits = 1;
                            else if (tpl[ti+3] >= '0' && tpl[ti+3] <= '9' && tpl[ti+4] === '}') digits = 2;
                        }
                        if (digits) {
                            const idx = parseInt(tpl.substr(ti + 2, digits), 10);
                            if (idx < _devSensors.length || idx < 16) {
                                const s = rec.slots[idx];
                                /* {pN} only resolves on the slot that actually
                                 * reports pressure — mirrors the firmware, which
                                 * has one rec.pressure and must not lend it to a
                                 * sensor that never measured it. */
                                let raw = null;
                                if (s && s.active) {
                                    if (ch === 't') raw = s.val;
                                    else if (ch === 'u') raw = s.hum;
                                    else if (s.hasPress) raw = rec.press;
                                }
                                val = (raw === undefined) ? null : raw;
                                hwid = s ? s.hwid : '';
                                compKey = ch + idx; tc = 2 + digits + 1;
                            }
                        }
                    }
                }
                if (tc === 0) { out += c; ti++; continue; }

                let mFull = false, mBare = false;
                if (compKey) {
                    const p1 = compKey.length + 6;
                    if (ti >= p1 && tpl.substr(ti - p1, p1) === '"' + compKey + '_ID":') mFull = true;
                    else {
                        const p2 = compKey.length + 3;
                        if (ti >= p2 && tpl.substr(ti - p2, p2) === '"' + compKey + '":') mBare = true;
                    }
                }

                if (mFull) {
                    out = out.substr(0, out.length - (compKey.length + 6));
                    if (val !== null) {
                        /* Letter from the token (t/u/p), matching the firmware. */
                        out += '"' + compKey[0] + (hwid || '').trim() + '":' + val;
                    }
                } else if (mBare) {
                    if (val !== null) out += val;
                    else out = out.substr(0, out.length - (compKey.length + 3));
                } else {
                    out += (val !== null ? val : 'null');
                }
                ti += tc;
            }

            /* In-place cleanup: collapse ",,", drop "{,", "[,", ",}", ",]" */
            let w = '';
            for (let i = 0; i < out.length; i++) {
                const ch = out[i];
                if (ch === ',') {
                    if (!w) continue;
                    const p = w[w.length-1];
                    if (p === ',' || p === '{' || p === '[') continue;
                } else if ((ch === '}' || ch === ']') && w && w[w.length-1] === ',') {
                    w = w.slice(0, -1);
                }
                w += ch;
            }
            return w;
        }

        /* Mirror of firmware buildPayload global-template walker. */
        function _previewGlobal(gt, dev, mac, data) {
            let out = '', gi = 0;
            while (gi < gt.length) {
                if (gt[gi] !== '{') { out += gt[gi]; gi++; continue; }
                if (gt.substr(gi, 5) === '{DEV}') { out += dev; gi += 5; }
                else if (gt.substr(gi, 5) === '{MAC}') { out += mac; gi += 5; }
                else if (gt.substr(gi, 6) === '{DATA}') { out += data; gi += 6; }
                else { out += gt[gi]; gi++; }
            }
            return out;
        }

        function renderPreview() {
            const mode = document.getElementById('t_mode').value;
            const pre = document.getElementById('preview');
            const batch = _previewDemoBatch();

            if (mode == '0') {
                /* JSON: [{"ts":T,"t<hwid|idx>":V,"u<hwid|idx>":H,...},{...}]
                 * Empty hwid falls back to slot index (mirrors firmware formatLineJsonBuf). */
                const lines = batch.map(r => {
                    let s = '{"ts":' + r.ts;
                    r.slots.forEach((sl, i) => {
                        if (!sl.active) return;
                        const id = sl.hwid ? sl.hwid : i;
                        s += ',"t' + id + '":' + sl.val;
                        if (sl.hum !== null) s += ',"u' + id + '":' + sl.hum;
                    });
                    const pSlot = r.slots.find(sl => sl.active && sl.hasPress && sl.hwid);
                    if (pSlot) s += ',"p' + pSlot.hwid + '":' + r.press;
                    return s + '}';
                });
                pre.innerText = '[' + lines.join(',') + ']';
            } else if (mode == '1') {
                /* CSV: header only active; each line emits all slot columns (empty if NaN).
                   The ambT;ambH pair left the header with the ambient slot. */
                let hdr = 'timestamp';
                batch[0].slots.forEach((sl, i) => { if (sl.active) hdr += ';s' + i + '_' + sl.hwid; });
                const lines = batch.map(r => {
                    let s = String(r.ts);
                    r.slots.forEach(sl => { s += ';' + (sl.active ? sl.val : ''); });
                    return s;
                });
                pre.innerText = hdr + '\n' + lines.join('\n') + '\n';
            } else if (mode == '2') {
                const glob = document.getElementById('t_glob').value;
                const line = document.getElementById('t_line').value;
                let sep = document.getElementById('t_sep').value;
                if (sep === '\\n') sep = '\n';
                const data = batch.map(r => _previewCustomLine(line, r)).join(sep);
                pre.innerText = _previewGlobal(glob, 'SIMUT_Demo', 'AA:BB:CC:DD:EE:FF', data);
            }
        }

        /* F-NET-TIME.5: banner de telemetria desabilitada quando t_int=0.
         * Chamado em loadConfig e a cada input no campo t_int. */
        function updateTelDisabledWarn() {
            const inp = document.getElementById('t_int');
            const warn = document.getElementById('tel_disabled_warn');
            if (!inp || !warn) return;
            const v = parseInt(inp.value, 10);
            warn.style.display = (!v || v === 0) ? '' : 'none';
        }

        /* F-NET-TIME.3b: Date & Time helpers. */
        function toggleManualTime() {
            const ntpOn = document.getElementById('ntp_enabled').checked;
            document.getElementById('manual_time_fields').style.opacity = ntpOn ? '0.5' : '1';
            /* Fields continuam habilitados mesmo com NTP on — user pode setar uma
             * vez; próximo sync NTP vai sobrescrever. Apenas visual degradado. */
        }

        async function applyManualTime() {
            const dateStr = document.getElementById('man_date').value;
            const timeStr = document.getElementById('man_time').value;
            const result = document.getElementById('time_result');
            /* Validação client-side: input date tem min=2026-01-01, então o browser
             * já rejeita anteriores — esta checagem cobre apenas "campo vazio". */
            if (!dateStr || !timeStr) {
                result.textContent = window.t('cfg_time_need', 'Fill date and time.');
                result.style.color = 'var(--dang)';
                return;
            }
            const tz = parseInt(document.getElementById('tz').value) || 0;
            const [y, m, d] = dateStr.split('-').map(Number);
            const tp = timeStr.split(':').map(Number);
            const h = tp[0] || 0, mi = tp[1] || 0, sec = tp[2] || 0;
            /* Interpreta como hora local do device (offset tz): epoch UTC = local - tz. */
            const utcMillis = Date.UTC(y, m - 1, d, h, mi, sec) - tz * 3600 * 1000;
            const epoch = Math.floor(utcMillis / 1000);
            try {
                const r = await fetch('/api/set_time', {
                    method: 'POST',
                    credentials: 'same-origin',
                    body: JSON.stringify({ epoch })
                });
                const resp = await r.json();
                if (resp.ok) {
                    result.textContent = window.t('cfg_time_ok', 'Time applied.');
                    result.style.color = 'var(--acc)';
                } else {
                    /* Erros de validação do back-end (epoch baixo demais, formato ruim)
                     * não devem ocorrer com min=2026-01-01 no input. Se aparecerem,
                     * mensagem genérica traduzida em vez do string cru em inglês. */
                    result.textContent = window.t('cfg_time_fail', 'Failed to apply.');
                    result.style.color = 'var(--dang)';
                }
            } catch (e) {
                result.textContent = window.t('cfg_time_fail', 'Failed to apply.');
                result.style.color = 'var(--dang)';
            }
        }

        /* Disables/enables every control inside the form. A failed load used to
         * leave the inputs enabled but unwired, so typing appeared to work and
         * silently went nowhere. Disabled + a visible banner is the honest
         * state, and it also stops a commit from writing blanks over flash. */
        function setFormEnabled(on) {
            const form = document.getElementById('sysForm');
            if (!form) return;
            form.querySelectorAll('input,select,textarea,button').forEach(el => { el.disabled = !on; });
            form.style.opacity = on ? '1' : '0.55';
        }

        function showLoadError(msgKey, fallback) {
            const box = document.getElementById('cfg_load_err');
            const msg = document.getElementById('cfg_load_err_msg');
            if (msg) msg.textContent = window.t ? window.t(msgKey, fallback) : fallback;
            if (box) box.style.display = '';
            setFormEnabled(false);
        }

        function clearLoadError() {
            const box = document.getElementById('cfg_load_err');
            if (box) box.style.display = 'none';
            setFormEnabled(true);
        }

        async function loadConfig() {
            /* fetchSafe retries the HTTP request, but a response that arrives
             * truncated still parses as a JSON error — and truncation is the
             * common failure here: /api/config streams chunked via safeSend,
             * and a Core-1 restart stalls Core 0 long enough for the socket to
             * break mid-body (logged server-side as WEB_CLIENT_DISCONNECT).
             * So retry the whole load, parse included. */
            for (let attempt = 0; attempt < 3; attempt++) {
                if (attempt > 0) await new Promise(r => setTimeout(r, 700 * attempt));
                try {
                    const r = await fetchSafe('/api/config');
                    if (r.status === 403) {
                        showLoadError('cfg_load_forbidden',
                            'Your user lacks permission to read the system settings.');
                        return;
                    }
                    if (r.status === 503) continue;   /* display busy — retry */
                    if (!r.ok) continue;
                    const d = await r.json();
                    if (d.error) {
                        showLoadError('cfg_load_forbidden',
                            'The device refused to return the settings: ' + d.error);
                        return;
                    }
                    applyConfig(d);
                    clearLoadError();
                    return;
                } catch (e) {
                    console.error('loadConfig attempt ' + attempt + ' failed:', e);
                }
            }
            showLoadError('cfg_load_fail',
                'Could not load the current settings. The fields are disabled to avoid saving blank values over your configuration.');
        }

        /* Populates the form from a fully-parsed /api/config payload. Split out
         * of loadConfig so a transport failure can be retried without the DOM
         * work, and so an exception in here is no longer indistinguishable from
         * a network failure. */
        function applyConfig(d) {
                /* U24: aplica valores da flash, depois sobrepõe pendentes
                 * do sessionStorage. Usuário vê o estado "provisório" que
                 * será aplicado no commit. */
                const p = Pending.getSection('sys');
                const val = (key, def) => (p[key] !== undefined ? p[key] : (d[key] !== undefined ? d[key] : def));
                const boolVal = (key, def) => (p[key] !== undefined) ? (p[key] !== '0') : (d[key] !== undefined ? !!d[key] : !!def);
                document.getElementById('name').value = val('name', '');
                document.getElementById('tz').value = val('tz', 0);
                document.getElementById('log').checked = !!val('log', false);
                /* F-NET-TIME.3b: NTP enable + preenche inputs de manual time com agora. */
                document.getElementById('ntp_enabled').checked = boolVal('ntp_enabled', true);
                const nowEpoch = d.now_epoch || Math.floor(Date.now() / 1000);
                const tzNow = parseInt(val('tz', 0)) || 0;
                const localMs = (nowEpoch + tzNow * 3600) * 1000;
                const nd = new Date(localMs);
                const pad = n => String(n).padStart(2, '0');
                document.getElementById('man_date').value = nd.getUTCFullYear() + '-' + pad(nd.getUTCMonth()+1) + '-' + pad(nd.getUTCDate());
                document.getElementById('man_time').value = pad(nd.getUTCHours()) + ':' + pad(nd.getUTCMinutes()) + ':' + pad(nd.getUTCSeconds());
                toggleManualTime();
                document.getElementById('res').value = val('res', 9);
                document.getElementById('s_int').value = val('s_int', 5000);
                document.getElementById('h_int').value = val('h_int', 1);
                document.getElementById('t_transport').value = val('t_transport', 0);
                document.getElementById('t_sec').checked = !!val('t_sec', false);
                window.__tlsCert = !!val('t_cert', false);   /* M-8: cert loaded at boot? */
                updateTlsWarn();
                document.getElementById('t_srv').value = val('t_srv', '');
                document.getElementById('t_port').value = val('t_port', 80);
                document.getElementById('t_path').value = val('t_path', '');
                document.getElementById('t_key').value = val('t_key', '');
                document.getElementById('m_topic').value = val('m_topic', '');
                document.getElementById('m_cid').value = val('m_cid', '');
                document.getElementById('m_user').value = val('m_user', '');
                document.getElementById('m_qos').value = val('m_qos', 0);
                document.getElementById('m_retain').checked = !!val('m_retain', false);
                document.getElementById('m_ka').value = val('m_ka', 60);
                document.getElementById('t_int').value = val('t_int', 300000);
                updateTelDisabledWarn();
                document.getElementById('t_bat').value = val('t_bat', 10);
                document.getElementById('t_mode').value = val('t_mode', 0);
                document.getElementById('t_glob').value = val('t_glob', '');
                document.getElementById('t_line').value = val('t_line', '');
                document.getElementById('t_sep').value = val('t_sep', '');
                if (d.serial) _devSerial = d.serial;
                if (Array.isArray(d.sensors)) {
                    for (let i = 0; i < 10 && i < d.sensors.length; i++) {
                        _devSensors[i] = { hwid: d.sensors[i].hwid || '', active: !!d.sensors[i].active,
                                           hasHum: !!d.sensors[i].hum, hasPress: !!d.sensors[i].press };
                    }
                }
                toggleTransport(); toggleBuilder();
                wirePendingListeners();
        }

        /* U24: cada input/change acumula no sessionStorage via Pending.
         * Fields com checkbox são normalizados para '0'/'1' pra espelhar
         * o que o endpoint /api/save_sys esperava (compat com parser do
         * server). */
        function wirePendingListeners() {
            const form = document.getElementById('sysForm');
            if (!form || form._pendingWired) return;
            form._pendingWired = true;
            const handler = (ev) => {
                const el = ev.target;
                if (!el.id) return;
                let v;
                if (el.type === 'checkbox') v = el.checked ? '1' : '0';
                else v = el.value;
                /* The sensor editor sits inside this form but stages itself,
                 * per slot, into Pending.slots. Without this guard every se_*
                 * field also landed in Pending.sys as a junk key. */
                if (el.closest && el.closest('#sens_ed')) return;
                Pending.setField('sys', el.id, v);
                /* F-NET-TIME.5: refresh hint quando user digita em t_int. */
                if (el.id === 't_int') updateTelDisabledWarn();
            };
            form.addEventListener('input', handler);
            form.addEventListener('change', handler);
        }

        async function resetTouchCal() {
            let msg = window.t('cfg_touch_confirm', 'Reset touch calibration to factory defaults?');
            if (!confirm(msg)) return;
            try {
                let r = await fetchSafe('/api/reset_touch_cal', { method: 'POST' });
                if (r.status === 503) { showToast(window.t('display_busy','Display in use. Try again shortly.'), 'warn'); return; }
                let j = await r.json();
                if (j.status === 'ok') showToast(window.t('cfg_touch_done', 'Reset done — calibration wizard is now running on the display.'), 'ok');
                else showToast('Error', 'err');
            } catch(e) { showToast('Error', 'err'); }
        }

        /* Sensor slot editor.
         * A slot is an index 0..15 into the persisted SensorRecord array, not a
         * GPIO: it owns 1..4 GPIOs. How many it needs and what each one does
         * come from the type catalogue in /api/sensors, which the firmware
         * generates from SensorFormat::forType -- the same table the driver
         * uses to configure the pads at boot. So this page cannot disagree with
         * the hardware about which pin of a BMP280 is SCL.
         * Edits stage into Pending.slots and reach flash on Save and Restart. */
        let SENS = null, CAL = null, sensOpenSlot = -1;
        /* Point-editor working state. calEdit holds the rows as the user
           typed them (strings, possibly incomplete), keyed slot:chkey, so a
           structural redraw does not lose a half-filled row. calDirty marks
           channels to stage — an untouched channel is never sent, which is
           what the server-side "absent means unchanged" rides on. calMode
           holds the interpolation choice per channel: lin or cub. */
        let calEdit = {}, calDirty = {}, calMode = {};
        const SE = id => document.getElementById(id);
        function sensStaged() { return Pending.getSection('slots').s || []; }
        function calStaged() { return (Pending.getSection('calib').sensors) || []; }
        /* /api/calib only reports active slots. */
        function calOf(i) { return CAL ? (CAL.sensors || []).find(x => x.slot === i) : null; }
        function sensSlot(i) {
            const st = sensStaged().find(x => x.i === i);
            return st ? Object.assign({}, SENS.slots[i], st) : SENS.slots[i];
        }
        function sensTypeOf(t) { return (SENS.types || []).find(x => x.t === t) || null; }

        async function loadSensors() {
            try { SENS = await (await fetchSafe('/api/sensors')).json(); renderSensors(); }
            catch (e) {
                SE('sens_err').style.display = '';
                SE('sens_err').textContent = window.t('sens_load_fail', 'Could not load the sensor slots.');
                return;
            }
            /* Calibration is a separate permission (PERM_CALIB) and a separate
               endpoint. A user with sys-config but not calib gets 403 here and
               simply sees no calibration panel, which is the correct RBAC
               outcome rather than an error. */
            try { CAL = await (await fetchSafe('/api/calib')).json(); renderSensors(); } catch (e) { CAL = null; }
        }

        /* Recomputed from the staged view, so the strip shows the layout that
           Save and Restart would produce, not the stored one. */
        function sensOwners() {
            const o = {};
            for (let i = 0; i < SENS.nslot; i++) {
                const s = sensSlot(i);
                /* First claim wins. Last-write-wins would let the slot being
                   edited silently take a GPIO off another slot in the map, so
                   the conflict only surfaced as a 400 after pressing Save. */
                if (s.a) (s.p || []).forEach(g => { if (g <= SENS.gmax && o[g] === undefined) o[g] = i; });
            }
            return o;
        }

        function renderSensors() {
            if (!SENS) return;
            const own = sensOwners();
            let g = '';
            for (let p = 0; p <= SENS.gmax; p++) {
                const u = own[p] !== undefined;
                /* O numero do slot era so `title`, e tooltip nao existe no toque:
                   no celular a unica forma de saber de quem era o GP7 era abrir
                   todos os slots. Agora vai visivel ao lado do pino. */
                g += '<span class="sxg' + (u ? ' on' : '') + '" title="' + (u ? 'slot ' + own[p] : window.t('sens_free', 'free')) + '">GP' + p + (u ? '<i>' + own[p] + '</i>' : '') + '</span>';
            }
            SE('sens_map').innerHTML = g;
            /* Only slots that exist as far as the user is concerned. An empty
               slot is an implementation detail of the fixed 16-entry array —
               listing all of them buried the four real sensors in twelve blank
               rows. New ones arrive through sensAdd. */
            let h = '', used = 0;
            for (let i = 0; i < SENS.nslot; i++) {
                const s = sensSlot(i);
                if (!s.a && !s.t) continue;
                used++;
                const ty = sensTypeOf(s.t);
                const pins = (s.p || []).filter(x => x <= SENS.gmax);
                const d = sensStaged().some(x => x.i === i);
                h += '<tr onclick="sensEdit(' + i + ')"' + (d ? ' style="background:rgba(6,182,212,.10)"' : '') + '>' +
                     '<td class="sxm">' + i + (s.a ? ' <span style="color:var(--ok)" title="' + window.t('sens_active', 'Active') + '">&#9679;</span>' : '') + '</td>' +
                     '<td>' + (ty ? ty.n : '&mdash;') + '</td>' +
                     '<td class="sxm">' + (pins.length ? pins.map(x => 'GP' + x).join(' ') : '&mdash;') + '</td>' +
                     '<td class="sxm">' + (s.hwId ? escHtml(s.hwId) : '&mdash;') + '</td>' +
                     '<td>' + escHtml(s.name || '') + '</td></tr>';
            }
            if (!used) h = '<tr><td colspan="5" class="sx-empty">' +
                           window.t('sens_none', 'No sensor slots configured yet.') + '</td></tr>';
            SE('sens_tb').innerHTML = h;
            const free = sensFirstFree();
            SE('sens_add').style.display = free < 0 ? 'none' : '';
            SE('sens_full').style.display = free < 0 ? '' : 'none';
            /* Deliberately does NOT redraw the open editor. Staging calls this
               on every keystroke to refresh the table and the GPIO strip, and
               rebuilding the editor markup here would replace the input the
               user is typing into and send the caret to the end. Redraws are
               explicit: sensStage(1) for controls that reshape the form. */
        }

        function sensFirstFree() {
            for (let i = 0; i < SENS.nslot; i++) { const s = sensSlot(i); if (!s.a && !s.t) return i; }
            return -1;
        }

        /* Clearing the markup matters: sensDrawEditor reads the live se_t value
           so a redraw keeps the half-made choice, and without this the controls
           of the previously open slot were still in the DOM and got read as if
           they belonged to the new one. */
        /* body overflow: sem a trava, arrastar sobre o modal rolava a pagina atras
           dele — e com a tabela de sensores rolando na horizontal dava para sair
           de lado com o dialogo aberto. */
        function sensEdit(i) { SE('sens_ed').innerHTML = ''; sensOpenSlot = i; sensDrawEditor(); SE('sens_ov').style.display = 'flex'; document.body.style.overflow = 'hidden'; }
        function sensClose() { sensOpenSlot = -1; SE('sens_ed').innerHTML = ''; SE('sens_ov').style.display = 'none'; document.body.style.overflow = ''; }
        /* Only a click on the backdrop itself closes; clicks inside the box
           bubble up to the same handler and must not. */
        function sensBackdrop(ev) { if (ev.target === SE('sens_ov')) sensClose(); }

        /* Adds the lowest free slot. The index is not cosmetic — it is the
           position in the persisted array and the identity the history and the
           CLI use, so it is shown rather than hidden behind a counter. */
        function sensAdd() {
            const i = sensFirstFree();
            if (i < 0) return;
            sensEdit(i);
        }

        /* Pin rows follow the type catalogue, so switching a slot from DHT22 to
           BMP280 grows the form from one Data row to SDA plus SCL with no table
           in this file to keep in step. */
        function sensDrawEditor() {
            const i = sensOpenSlot, s = sensSlot(i), own = sensOwners();
            const t = SE('se_t') ? parseInt(SE('se_t').value) : s.t;
            const ty = sensTypeOf(t), nP = ty ? ty.pins.length : 0;
            let h = '<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:14px"><span class="sx-slot">' +
                    window.t('sens_slot', 'Slot') + ' ' + i + '</span>' +
                    '<button type="button" class="sxb" onclick="sensClose()" aria-label="Close">&times;</button></div>' +
                    '<div class="row"><div class="col"><label>' + window.t('sens_type', 'Type') + '</label>' +
                    '<select id="se_t" onchange="sensStage(1)"><option value="0"' + (t === 0 ? ' selected' : '') +
                    '>&mdash; ' + window.t('sens_empty', 'empty') + ' &mdash;</option>';
            (SENS.types || []).forEach(x => {
                h += '<option value="' + x.t + '"' + (t === x.t ? ' selected' : '') + '>' + escHtml(x.n) + '</option>';
            });
            h += '</select></div><div class="col"><label>' + window.t('sens_name', 'Name') + '</label>' +
                 '<input id="se_n" type="text" oninput="sensStage(0)" maxlength="31" value="' + escHtml(s.name || '') + '"></div></div>' +
                 '<div class="row"><div class="col"><label>' + window.t('sens_hwid', 'Hardware ID') + '</label>' +
                 '<input id="se_id" type="text" oninput="sensStage(0)" maxlength="15" value="' + escHtml(s.hwId || '') + '">' +
                 '<div class="sxn">' + window.t('sens_hwid_hint', 'Key used by history and telemetry. Changing it starts a new series.') + '</div></div>' +
                 '<div class="col"><label>' + window.t('sens_state', 'State') + '</label>' +
                 '<label class="cfg-tg"><input type="checkbox" id="se_a" onchange="sensStage(1)"' + (s.a ? ' checked' : '') + '> <span>' + window.t('sens_active', 'Active') + '</span></label>' +
                 '<label class="cfg-tg"><input type="checkbox" id="se_al" onchange="sensStage(0)"' + (s.al ? ' checked' : '') + '> <span>' + window.t('sens_alarms', 'Alarms enabled') + '</span></label></div></div>';
            if (nP) {
                h += '<label class="sxsec">' + window.t('sens_pins', 'GPIO assignment') + '</label><div class="row">';
                for (let k = 0; k < nP; k++) {
                    const cur = (s.p && s.p[k] !== undefined) ? s.p[k] : 255;
                    h += '<div class="col"><div class="sxh">' + window.t('sens_pin', 'Pin') + ' ' + k +
                         ' &mdash; <strong style="color:var(--acc)">' + escHtml(ty.pins[k]) + '</strong></div>' +
                         '<select id="se_p' + k + '" onchange="sensStage(1)"><option value="255"' + (cur > SENS.gmax ? ' selected' : '') + '>&mdash;</option>';
                    for (let gp = 0; gp <= SENS.gmax; gp++) {
                        const tk = own[gp] !== undefined && own[gp] !== i;
                        h += '<option value="' + gp + '"' + (cur === gp ? ' selected' : '') + (tk ? ' disabled' : '') +
                             '>GP' + gp + (tk ? ' (slot ' + own[gp] + ')' : '') + '</option>';
                    }
                    h += '</select></div>';
                }
                h += '</div>';
            }
            /* Limites de alarme saem daqui: moram na /alarms, que os edita e ainda
               acopla min<max, coisa que este formulario nunca fez. Duplicar dava
               dois lugares para o mesmo dado, com validacoes diferentes.
               O payload de sensStage repassa `lim` intacto — o objeto por canal
               que a /api/sensors devolve — para preservar o que ja esta gravado
               em vez de zerar. Antes eram quatro chaves tmin/tmax/hmin/hmax, que
               nao tinham como carregar o limite de uma terceira grandeza.
               NAO mexer no se_al, que fica na secao acima — ele e lido sem guarda
               de nulo em sensStage e sumir dele quebraria o salvamento. */

            /* Calibration. Up to 5 (raw, reference) points per quantity — the
               firmware interpolates the correction between them and holds it
               flat beyond the ends; one point is the old constant offset. The
               data is not a SensorRecord field: it lives in calib.csv, keyed
               by the 1-Wire ROM for a DS18B20 and by board serial + hwId for
               a sensor that has no ROM.
               Driven by c.channels, which the firmware builds from the
               channel table — one block per quantity this sensor reports, so
               a new quantity appears here with no page change at all. Rows
               render from calEdit (the typed strings), never from the staged
               numbers: a redraw mid-edit must not eat a half-filled row. */
            const c = calOf(i);
            if (c) {
                const stc = calStaged().find(x => x.slot === i);
                const chans = c.channels || [];
                h += '<label class="sxsec">' + window.t('sens_calib', 'Calibration') + '</label>' +
                     '<div class="sxn">' + window.t('cal_pts_hint',
                        'Up to 5 points per quantity, each pairing the raw reading with the value a trusted instrument showed. One point applies a constant offset; more points bend the correction between them, held flat beyond the ends. Leave raw empty to capture the reading at save time.') + '</div>';
                chans.forEach(ch => {
                    const ek = i + ':' + ch.key;
                    const stv = (stc && stc.cal) ? stc.cal[ch.key] : undefined;
                    if (calEdit[ek] === undefined) {
                        /* A staged channel may be the plain array (linear) or
                           the object form with an interpolation mode. */
                        const src = (stv !== undefined)
                            ? (Array.isArray(stv) ? stv : (stv.p || []))
                            : (ch.pts || []);
                        calEdit[ek] = src.map(p => ({ r: p[0] === null ? '' : String(p[0]), v: String(p[1]) }));
                        /* Staged rows that survived a page reload are still
                           edits — without this they would stop being sent. */
                        if (stv !== undefined) calDirty[ek] = 1;
                    }
                    if (calMode[ek] === undefined) {
                        calMode[ek] = (stv !== undefined && !Array.isArray(stv) && stv.m === 'cub') ? 'cub'
                                    : (ch.mode === 'cub' ? 'cub' : 'lin');
                    }
                    const rows = calEdit[ek];
                    const step = ch.dec >= 2 ? '0.01' : (ch.dec === 1 ? '0.1' : '1');
                    const legacy = !calDirty[ek] && rows.length === 0 && (ch.pts || []).length === 0 && Math.abs(ch.offset) >= 0.005;
                    h += '<div class="sxh" style="margin-top:10px"><strong>' + window.t(ch.label, ch.key) + '</strong> &mdash; ' +
                         window.t('cal_raw', 'Raw') + ': <strong>' + (ch.raw === null ? '--' : ch.raw + ' ' + ch.unit) + '</strong>' +
                         ' &rarr; ' + window.t('cal_corr', 'corrected') + ': <strong>' + (ch.read === null ? '--' : ch.read + ' ' + ch.unit) + '</strong></div>' +
                         '<svg class="spk" id="se_spk_' + ch.key + '" viewBox="0 0 560 74" preserveAspectRatio="none" aria-hidden="true"></svg>';
                    if (!rows.length) {
                        h += '<div class="sxn">' + (legacy
                             ? window.t('cal_legacy', 'Constant offset') + ': ' + ch.offset + ' ' + ch.unit
                             : window.t('cal_none', 'No correction — sensor default.')) + '</div>';
                    }
                    rows.forEach((r, idx) => {
                        h += '<div class="sxpt">' +
                             '<input id="se_pr_' + ch.key + '_' + idx + '" type="number" step="' + step + '" placeholder="' +
                             window.t('cal_raw', 'Raw') + '" value="' + r.r + '" oninput="calPtIn(&quot;' + ch.key + '&quot;,' + idx + ',this)">' +
                             '<input id="se_pv_' + ch.key + '_' + idx + '" type="number" step="' + step + '" placeholder="' +
                             window.t('cal_ref', 'Reference') + '" value="' + r.v + '" oninput="calPtIn(&quot;' + ch.key + '&quot;,' + idx + ',this)">' +
                             '<button type="button" class="sxb" title="' + window.t('cal_cap', 'Use the current raw reading') + '"' +
                             (ch.raw === null ? ' disabled' : '') + ' onclick="calPtCap(&quot;' + ch.key + '&quot;,' + idx + ')">&#8635;</button>' +
                             '<button type="button" class="sxb sxb-dang" title="' + window.t('cal_del', 'Remove this point') + '"' +
                             ' onclick="calPtDel(&quot;' + ch.key + '&quot;,' + idx + ')">&times;</button></div>';
                    });
                    h += '<div style="display:flex;gap:8px;margin-top:4px;flex-wrap:wrap;align-items:center">';
                    if (rows.length < 5) h += '<button type="button" class="sxb" onclick="calPtAdd(&quot;' + ch.key + '&quot;)">+ ' + window.t('cal_add', 'Add point') + '</button>';
                    if (rows.length || legacy) h += '<button type="button" class="sxb sxb-dang" onclick="calPtClear(&quot;' + ch.key + '&quot;)">' + window.t('cal_clear', 'Remove correction') + '</button>';
                    /* Interpolation choice — always visible, one per quantity
                       of every sensor type. Below 3 points the cubic IS the
                       straight line, so the choice is stored but changes
                       nothing yet; the hint says so. */
                    {
                        const mc = calMode[ek] === 'cub';
                        h += '<span class="sxh" style="margin:0 0 0 6px">' + window.t('cal_mode_lbl', 'Interpolation') + ':</span>' +
                             '<button type="button" class="sxb' + (mc ? '' : ' sxb-on') + '" onclick="calModeSet(&quot;' + ch.key + '&quot;,&quot;lin&quot;)">' + window.t('cal_mode_lin', 'Straight') + '</button>' +
                             '<button type="button" class="sxb' + (mc ? ' sxb-on' : '') + '" onclick="calModeSet(&quot;' + ch.key + '&quot;,&quot;cub&quot;)">' + window.t('cal_mode_cub', 'Smooth') + '</button>';
                    }
                    h += '</div>';
                    if (calMode[ek] === 'cub' && rows.length < 3) {
                        h += '<div class="sxn">' + window.t('cal_mode_hint', 'Smooth is a monotone cubic: it bends through the anchors without ever overshooting them. Needs 3+ points; with fewer it behaves as straight.') + '</div>';
                    }
                });
                h += '<div id="se_calwarn" class="sx-warn"></div>' +
                     '<div class="sxn">' + window.t('cal_save_hint', 'Corrections are written on Save and Restart. Needs NTP synced.') +
                     (CAL && CAL.ntp === false ? ' <span style="color:var(--dang)">' + window.t('sens_ntp_no', 'NTP not synced.') + '</span>' : '') + '</div>';
            }

            /* History rebind. Device-wide, not slot-scoped — hence its own
               section and the "all slots" wording. It lives here because this
               is where you are standing when you notice a slot you just added
               is not being recorded: the .sim4 of the day froze its schema
               before the slot existed, so its channel is written as NaN and
               nothing complains. Same operation as `sensor reschema confirm`.
               NOTE: no apostrophes in comments on this page — the minifier
               mis-parses them and the node --check guard fails the build. */
            h += '<label class="sxsec">' + window.t('sens_hist', 'History recording') + '</label>' +
                 '<div class="sxn">' + window.t('sens_rebind_hint',
                    'A slot added or renamed today has no column in the history file of the day, so it is not recorded until tomorrow. This rewrites that file for all slots, keeping the records already taken. The device restarts at the end.') + '</div>' +
                 '<button type="button" class="sxb" id="se_rebind" onclick="histRebind()" style="margin-top:10px">' +
                 window.t('sens_rebind', 'Rebind history now') + '</button>';

            /* Hardware operations. These act on the device NOW — unlike every
               other control on this page they are not staged for Save and
               Restart, because adopting a probe or moving an epoch has to
               happen against the hardware as it is at this instant.
               Adopt only exists where a factory serial exists to adopt
               (ty.sn — DS18B20 ROM): a serial-less part cannot error when
               moved to the wrong place, so offering the button there only
               invited the LIB_SENS rename accident. */
            h += '<label class="sxsec">' + window.t('sens_hw', 'Hardware') + '</label>';
            if (ty && ty.sn) {
                h += '<div class="sxn">' + window.t('sens_accept_hint',
                        'Re-reads the probe wired to this slot and binds the slot to it. Use it after swapping a DS18B20, when the device reports a hardware mismatch.') + '</div>' +
                     '<button type="button" class="sxb" id="se_adopt" onclick="sensAdopt()" style="margin-top:10px">' +
                     window.t('sens_accept', 'Adopt the probe wired here') + '</button>';
            }
            h += '<div class="sxn" style="margin-top:14px">' + window.t('sens_wipe_hint',
                    'Marks everything recorded before now as belonging to the previous sensor. The records stay on disk; the graphs stop attributing them to this slot.') + '</div>' +
                 '<button type="button" class="sxb sxb-dang" id="se_wipe" onclick="sensWipe()" style="margin-top:10px">' +
                 window.t('sens_wipe', 'Reset history epoch') + '</button>';

            h += '<div id="se_warn" class="sx-warn"></div>' +
                 '<div style="margin-top:16px;display:flex;gap:8px;flex-wrap:wrap">' +
                 '<button type="button" class="sxb sxb-dang" onclick="sensClear()">' + window.t('sens_free_slot', 'Free slot') + '</button>' +
                 '<button type="button" class="sxb" onclick="sensRevert()">' + window.t('sens_revert', 'Discard staged') + '</button>' +
                 '<button type="button" class="sxb" onclick="sensClose()">' + window.t('sens_done', 'Done') + '</button></div>' +
                 '<div class="sxn" style="margin-top:10px">' + window.t('sens_stage_hint', 'Edits are staged as you type and written on Save and Restart.') + '</div>';
            SE('sens_ed').innerHTML = h;
            sensWarn();
            calSparkAll();
        }

        /* Rebuilds the history file of the day against the SAVED slots.
         *
         * The Pending guard is not politeness: rebindV4Schema reads the slots
         * from flash, so running it with staged edits would freeze the old
         * schema again and the user would be back where they started, having
         * spent the records of the day for nothing.
         *
         * retries:0 on purpose — fetchSafe retries twice by default and this
         * is a destructive mutation. 30 s because recreating the day file
         * erases flash, and the firmware's own window for it is 30 s; the
         * 15 s default would report a failure over an operation still running. */
        /* POST /api/action — the maintenance operations that used to live only
           on the serial CLI. They bypass the staging buffer on purpose: each
           one reads or writes hardware state at this instant, so staging it
           for Save and Restart would apply it against a different reality. */
        async function sensAction(op, extra) {
            const q = '/api/action?op=' + encodeURIComponent(op) + (extra || '');
            const r = await fetch(q, { method: 'POST' });
            let j = {};
            try { j = await r.json(); } catch (e) { /* keep {} */ }
            return { ok: r.ok, status: r.status, body: j };
        }

        async function sensScan() {
            const btn = SE('sens_scan'), out = SE('sens_scan_out');
            if (!btn || !out) return;
            btn.disabled = true;
            out.textContent = window.t('sens_scan_busy', 'Scanning...');
            try {
                const start = await sensAction('sensor_scan');
                if (!start.ok && start.status !== 202) throw new Error('start');
                /* The scan is a state machine stepped by the main loop, so the
                   POST only arms it. Poll until it reports done — 20 tries at
                   500 ms covers the 16-pin sweep with margin. */
                for (let i = 0; i < 20; i++) {
                    await new Promise(res => setTimeout(res, 500));
                    const p = await sensAction('scan_results');
                    if (p.ok && p.body.scanning === false) {
                        const found = p.body.found || [];
                        if (!found.length) { out.textContent = window.t('sens_scan_none', 'No probes answered.'); return; }
                        out.textContent = found.length + ' ' + window.t('sens_scan_found', 'found') + ': ' +
                            found.map(f => 'GP' + f.pin + (f.rom ? ' (' + f.rom + ')' : '')).join(', ');
                        return;
                    }
                }
                out.textContent = window.t('sens_scan_none', 'No probes answered.');
            } catch (e) {
                out.textContent = window.t('act_fail', 'Action failed.');
            } finally {
                btn.disabled = false;
            }
        }

        async function sensAdopt() {
            const i = sensOpenSlot, btn = SE('se_adopt');
            if (i < 0 || !btn) return;
            btn.disabled = true;
            try {
                const r = await sensAction('sensor_accept', '&slot=' + i);
                if (r.ok) {
                    showToast(window.t('sens_accept_ok', 'Slot bound to the probe found on it.'), 'ok');
                    if (typeof loadSensors === 'function') loadSensors();
                } else if (r.status === 404) {
                    showToast(window.t('sens_accept_none', 'No probe answered on this GPIO.'), 'err');
                } else if (r.status === 422) {
                    showToast(window.t('sens_accept_bad', 'The probe answered with an invalid ROM.'), 'err');
                } else {
                    showToast(window.t('act_fail', 'Action failed.'), 'err');
                }
            } finally { btn.disabled = false; }
        }

        async function sensWipe() {
            const i = sensOpenSlot, btn = SE('se_wipe');
            if (i < 0 || !btn) return;
            if (!confirm(window.t('sens_wipe_confirm',
                'Reset the history epoch of this slot?\n\nRecords taken before now stop being attributed to it. Nothing is deleted.'))) return;
            btn.disabled = true;
            try {
                const r = await sensAction('sensor_wipe', '&slot=' + i);
                showToast(r.ok ? window.t('sens_wipe_ok', 'History epoch reset.')
                               : window.t('act_fail', 'Action failed.'), r.ok ? 'ok' : 'err');
            } finally { btn.disabled = false; }
        }

        async function telSync() {
            const b = SE('tel_sync_btn');
            if (b) b.disabled = true;
            try {
                const r = await sensAction('tel_sync');
                showToast(r.ok ? window.t('tel_sync_ok', 'Upload triggered. Watch the log for the result.')
                               : window.t('act_fail', 'Action failed.'), r.ok ? 'ok' : 'err');
            } finally { if (b) b.disabled = false; }
        }

        async function telReset() {
            if (!confirm(window.t('tel_reset_confirm',
                'Reset the telemetry cursor?\n\nThe device will re-send up to 30 days of history on the next upload. Expect a burst of traffic.'))) return;
            const b = SE('tel_reset_btn');
            if (b) b.disabled = true;
            try {
                const r = await sensAction('tel_reset');
                showToast(r.ok ? window.t('tel_reset_ok', 'Cursor reset. Next uploads cover up to 30 days back.')
                               : window.t('act_fail', 'Action failed.'), r.ok ? 'ok' : 'err');
            } finally { if (b) b.disabled = false; }
        }

        async function histRebind() {
            if (Pending.hasAny()) {
                showToast(window.t('sens_rebind_pending',
                    'Save and Restart first — rebinding now would use the previous configuration.'), 'warn', 6000);
                return;
            }
            if (!confirm(window.t('sens_rebind_confirm',
                'Rewrite the history file of the day for the current slots?\n\nThe records already taken are kept. The device restarts at the end.'))) return;
            await histRebindPost(false);
        }

        /* Split out because the destructive retry re-enters with force=1. The
           two calls differ only in the query string and in what was confirmed. */
        async function histRebindPost(force) {
            const b = SE('se_rebind');
            if (b) { b.disabled = true; b.textContent = window.t('sens_rebind_busy', 'Rewriting...'); }
            try {
                /* 90 s: the rewrite streams the whole day twice — once to write
                   the replacement, once to verify it against the source — and a
                   full day at the 1-minute interval is up to 1440 records. */
                const r = await fetchSafe('/api/history_rebind' + (force ? '?force=1' : ''),
                                          { method: 'POST', timeout: 90000, retries: 0 });
                if (r.status === 503) { showToast(window.t('display_busy', 'Display in use. Try again shortly.'), 'warn'); return; }
                const j = await r.json();
                if (j.status === 'ok') {
                    const msg = j.forced
                        ? window.t('sens_rebind_ok_forced', 'History rebuilt') + ' — ' + j.meas + ' ' + window.t('sens_rebind_meas', 'measurements')
                        : window.t('sens_rebind_ok', 'History rewritten') + ' — ' + j.recs + ' ' +
                          window.t('sens_rebind_recs', 'records kept') + ', ' + j.meas + ' ' +
                          window.t('sens_rebind_meas', 'measurements');
                    showToast(msg + '. ' + window.t('sens_rebind_reboot', 'Restarting...'), 'ok', 9000);
                    /* The device is already on its way down; the response was
                       sent before the reset. Give it the boot window, then reload. */
                    setTimeout(() => location.reload(), 25000);
                    return;
                }
                if (j.canForce) {
                    /* Migration could not read the source. Offer the destructive
                       path explicitly — never take it on behalf of the user. */
                    if (confirm(window.t('sens_rebind_force',
                        'The history file of the day could not be read, so the records cannot be carried over.\n\nRecreate it empty? The records of today are lost. Earlier days are untouched.'))) {
                        await histRebindPost(true);
                        return;
                    }
                    showToast(window.t('sens_rebind_kept', 'Nothing changed.'), 'warn');
                    return;
                }
                showToast(window.t('sens_rebind_err', 'Rewrite failed'), 'err');
            } catch (e) {
                /* Includes the timeout case, where the rewrite may well have
                   completed. Say so instead of inviting a blind second click. */
                showToast(window.t('sens_rebind_unsure',
                    'No answer from the device. Reload the page and check the log before trying again.'), 'err', 8000);
            } finally {
                if (SE('se_rebind')) sensDrawEditor();
            }
        }

        function sensSet(list) {
            /* Empty list drops the section: Pending.hasAny counts keys, so
               leaving {"s":[]} would keep Save and Restart lit with nothing staged. */
            Pending.setSection('slots', list.length ? { s: list.sort((a, b) => a.i - b.i) } : {});
            renderSensors();
        }
        function sensPut(e) { sensSet(sensStaged().filter(x => x.i !== e.i).concat([e])); }

        /* ── Calibration point editor ──
           The DOM inputs are transcribed into calEdit on every keystroke and
           the structural actions (add, remove, clear) mutate calEdit and ask
           for a redraw — the same split the rest of the editor uses. */
        function calPtIn(key, idx, el) {
            const ek = sensOpenSlot + ':' + key;
            if (!calEdit[ek] || !calEdit[ek][idx]) return;
            calEdit[ek][idx][el.id.indexOf('se_pr_') === 0 ? 'r' : 'v'] = el.value;
            calDirty[ek] = 1;
            sensStage(0);
        }
        function calPtCap(key, idx) {
            const c = calOf(sensOpenSlot);
            const ch = c ? (c.channels || []).find(x => x.key === key) : null;
            if (!ch || ch.raw === null) return;
            const ek = sensOpenSlot + ':' + key;
            if (!calEdit[ek] || !calEdit[ek][idx]) return;
            calEdit[ek][idx].r = String(ch.raw);
            const el = SE('se_pr_' + key + '_' + idx);
            if (el) el.value = ch.raw;
            calDirty[ek] = 1;
            sensStage(0);
        }
        function calPtAdd(key) {
            const ek = sensOpenSlot + ':' + key;
            calEdit[ek] = calEdit[ek] || [];
            if (calEdit[ek].length >= 5) return;
            calEdit[ek].push({ r: '', v: '' });
            calDirty[ek] = 1;
            sensStage(1);
        }
        function calPtDel(key, idx) {
            const ek = sensOpenSlot + ':' + key;
            if (!calEdit[ek]) return;
            calEdit[ek].splice(idx, 1);
            calDirty[ek] = 1;
            sensStage(1);
        }
        function calPtClear(key) {
            const ek = sensOpenSlot + ':' + key;
            calEdit[ek] = [];
            calDirty[ek] = 1;
            sensStage(1);
        }
        function calModeSet(key, m) {
            const ek = sensOpenSlot + ':' + key;
            calMode[ek] = m;
            calDirty[ek] = 1;
            sensStage(1);
        }
        /* ── Mini grafico da correcao ──
           Um por canal, dentro do editor: a reta do padrao (delta zero) e a
           curva de correcao com as ancoras, simulada no modo escolhido (reta
           ou suave — mesmo Fritsch-Carlson do firmware). Redesenha apenas o
           svg proprio, nunca os inputs — regra do caret. */
        function calSparkPts(ek) {
            const out = [];
            (calEdit[ek] || []).forEach(r => {
                if (r.v === '' || isNaN(parseFloat(r.v))) return;
                if (r.r === '' || isNaN(parseFloat(r.r))) return;
                out.push({ r: parseFloat(r.r), d: parseFloat(r.v) - parseFloat(r.r) });
            });
            out.sort((a, b) => a.r - b.r);
            return out;
        }
        function calSparkSlopes(pp) {
            const n = pp.length, m = new Array(n).fill(0);
            if (n < 3) return m;
            const h = [], d = [];
            for (let i = 0; i < n - 1; i++) { h.push(pp[i + 1].r - pp[i].r); d.push((pp[i + 1].d - pp[i].d) / h[i]); }
            for (let i = 1; i < n - 1; i++) {
                if (d[i - 1] === 0 || d[i] === 0 || ((d[i - 1] > 0) !== (d[i] > 0))) m[i] = 0;
                else m[i] = 3 * (h[i - 1] + h[i]) / ((2 * h[i] + h[i - 1]) / d[i - 1] + (h[i] + 2 * h[i - 1]) / d[i]);
            }
            return m;
        }
        function calSparkDelta(pp, m, smooth, x) {
            const n = pp.length;
            if (!n) return 0;
            if (n === 1 || x <= pp[0].r) return pp[0].d;
            if (x >= pp[n - 1].r) return pp[n - 1].d;
            for (let i = 1; i < n; i++) {
                if (x <= pp[i].r) {
                    const h = pp[i].r - pp[i - 1].r, t = (x - pp[i - 1].r) / h;
                    if (smooth && n >= 3) {
                        const t2 = t * t, t3 = t2 * t;
                        return pp[i - 1].d * (2 * t3 - 3 * t2 + 1) + h * m[i - 1] * (t3 - 2 * t2 + t) +
                               pp[i].d * (-2 * t3 + 3 * t2) + h * m[i] * (t3 - t2);
                    }
                    return pp[i - 1].d + t * (pp[i].d - pp[i - 1].d);
                }
            }
            return pp[n - 1].d;
        }
        function calSpark(ch) {
            const host = SE('se_spk_' + ch.key);
            if (!host) return;
            const ek = sensOpenSlot + ':' + ch.key;
            const pp = calSparkPts(ek);
            const smooth = calMode[ek] === 'cub';
            const legacyOff = (!pp.length && !calDirty[ek]) ? (ch.offset || 0) : 0;
            const W = 560, H = 74, L = 8, R = 8, T = 10, B = 12;
            let lo, hi;
            if (pp.length >= 2) {
                const sp = Math.max(pp[pp.length - 1].r - pp[0].r, 1);
                lo = pp[0].r - sp * 0.18; hi = pp[pp.length - 1].r + sp * 0.18;
            } else if (pp.length === 1) { lo = pp[0].r - 5; hi = pp[0].r + 5; }
            else if (ch.raw !== null) { lo = ch.raw - 5; hi = ch.raw + 5; }
            else { lo = ch.min; hi = ch.max; }
            let ym = 0.5;
            pp.forEach(p => { ym = Math.max(ym, Math.abs(p.d) * 1.3); });
            ym = Math.max(ym, Math.abs(legacyOff) * 1.3);
            const X = x => L + (x - lo) / (hi - lo) * (W - L - R);
            const Y = d => T + (ym - d) / (2 * ym) * (H - T - B);
            const m = calSparkSlopes(pp);
            let s = '';
            if (pp.length >= 2) {
                s += '<rect class="hz" x="' + L + '" y="' + T + '" width="' + (X(pp[0].r) - L).toFixed(1) + '" height="' + (H - T - B) + '"/>';
                s += '<rect class="hz" x="' + X(pp[pp.length - 1].r).toFixed(1) + '" y="' + T + '" width="' + (W - R - X(pp[pp.length - 1].r)).toFixed(1) + '" height="' + (H - T - B) + '"/>';
            }
            s += '<line class="l0" x1="' + L + '" y1="' + Y(0).toFixed(1) + '" x2="' + (W - R) + '" y2="' + Y(0).toFixed(1) + '"/>';
            if (pp.length || legacyOff) {
                let dp = '';
                const steps = 52;
                for (let k = 0; k <= steps; k++) {
                    const x = lo + (hi - lo) * k / steps;
                    const d = pp.length ? calSparkDelta(pp, m, smooth, x) : legacyOff;
                    dp += (k ? 'L' : 'M') + X(x).toFixed(1) + ' ' + Y(d).toFixed(1);
                }
                s += '<path class="lc" d="' + dp + '"/>';
            }
            if (ch.raw !== null && ch.raw >= lo && ch.raw <= hi) {
                s += '<line class="tk" x1="' + X(ch.raw).toFixed(1) + '" y1="' + T + '" x2="' + X(ch.raw).toFixed(1) + '" y2="' + (H - B) + '"/>';
            }
            pp.forEach(p => { s += '<circle class="pa" cx="' + X(p.r).toFixed(1) + '" cy="' + Y(p.d).toFixed(1) + '" r="3.5"/>'; });
            host.innerHTML = s;
        }
        function calSparkAll() {
            const c = (typeof calOf === 'function') ? calOf(sensOpenSlot) : null;
            if (c) (c.channels || []).forEach(calSpark);
        }
        /* Mirror of the rules POST /api/calib enforces, surfaced while typing
           instead of as a 400 after Save. First problem wins, like sensWarn. */
        function calWarnMsg() {
            const i = sensOpenSlot, c = calOf(i);
            if (!c) return '';
            const chans = c.channels || [];
            for (let k = 0; k < chans.length; k++) {
                const ch = chans[k], ek = i + ':' + ch.key;
                if (!calDirty[ek]) continue;
                const rows = (calEdit[ek] || []).filter(r => r.r !== '' || r.v !== '');
                const lbl = window.t(ch.label, ch.key);
                if (rows.length > 5) return lbl + ': ' + window.t('cal_err_max', 'at most 5 points.');
                const seen = [];
                for (let j = 0; j < rows.length; j++) {
                    const r = rows[j];
                    if (r.v === '' || isNaN(parseFloat(r.v)) || (r.r !== '' && isNaN(parseFloat(r.r))))
                        return lbl + ': ' + window.t('cal_err_num', 'every point needs numeric raw and reference values.');
                    if (r.r === '' && ch.raw === null)
                        return lbl + ': ' + window.t('cal_err_noread', 'no live reading to capture — fill the raw value.');
                    const rv = r.r === '' ? ch.raw : parseFloat(r.r), fv = parseFloat(r.v);
                    if (rv < ch.min || rv > ch.max || fv < ch.min || fv > ch.max)
                        return lbl + ': ' + window.t('cal_err_rng', 'point outside the plausible range') + ' (' + ch.min + '..' + ch.max + ').';
                    const rk = Math.round(rv * 100);
                    if (seen.indexOf(rk) >= 0)
                        return lbl + ': ' + window.t('cal_err_dup', 'two points share the same raw value.');
                    seen.push(rk);
                }
            }
            return '';
        }

        /* Reads the open editor and writes it straight into Pending.
         *
         * Every other section of this page stages on each keystroke, via the
         * delegated listener on #sysForm. This editor opted out of that
         * listener (it needs whole-slot objects, not flat id/value pairs), so
         * it has to stage itself the same way. It used to require pressing a
         * Stage button first, and anyone who edited the fields and went
         * straight to Save and Restart silently lost the edit -- the page
         * looked like it had saved and the slot came back unchanged.
         *
         * redraw=1 for controls that change the shape of the form (type, pins,
         * active). Text and number fields pass 0: redrawing on every keystroke
         * would rebuild the inputs and throw the caret to the end. */
        function sensStage(redraw) {
            const i = sensOpenSlot;
            if (i < 0 || !SE('se_t')) return;
            const t = parseInt(SE('se_t').value), ty = sensTypeOf(t);
            const nP = ty ? ty.pins.length : 0, p = [255, 255, 255, 255];
            for (let k = 0; k < nP; k++) if (SE('se_p' + k)) p[k] = parseInt(SE('se_p' + k).value);
            const b = SENS.slots[i];
            sensPut({ i: i, a: SE('se_a').checked, t: t, p: p,
                      hwId: SE('se_id').value.trim(), name: SE('se_n').value.trim(),
                      lim: b.lim, al: SE('se_al').checked });

            /* Calibration rides the pre-existing Pending.calib section, which
               commitAll POSTs to /api/calib before saving. Staged per channel
               and only for channels the user touched (calDirty): an absent
               channel means "unchanged" server-side, which is what lets a
               rename commit without re-sending curves it never edited. A
               channel with an incomplete or non-numeric row stages nothing —
               the red #se_calwarn line is already saying why. An empty row
               list IS staged: [] is the explicit "remove the correction". */
            const cal = (typeof calOf === 'function') ? calOf(i) : null;
            const chans = (cal && cal.channels) ? cal.channels : [];
            if (chans.length) {
                const rest = calStaged().filter(x => x.slot !== i);
                const entry = { slot: i, cal: {} };
                let anyc = 0;
                chans.forEach(ch => {
                    const ek = i + ':' + ch.key;
                    if (!calDirty[ek]) return;
                    const rows = (calEdit[ek] || []).filter(r => r.r !== '' || r.v !== '');
                    const pts = [];
                    let bad = false;
                    rows.forEach(r => {
                        const fv = parseFloat(r.v);
                        const rv = r.r === '' ? null : parseFloat(r.r);
                        if (r.v === '' || isNaN(fv) || (rv !== null && isNaN(rv))) { bad = true; return; }
                        pts.push([rv, fv]);
                    });
                    if (bad || pts.length > 5) return;
                    entry.cal[ch.key] = (calMode[ek] === 'cub') ? { m: 'cub', p: pts } : pts;
                    anyc = 1;
                });
                if (anyc) rest.push(entry);
                Pending.setSection('calib', rest.length ? { sensors: rest } : {});
            }
            if (redraw) { sensDrawEditor(); } else { sensWarn(); calSparkAll(); }
        }

        /* Live echo of the rule commit_all enforces server-side. Shown while
           editing instead of only as a rejection after pressing Save. */
        function sensWarn() {
            const el = SE('se_warn');
            if (!el) return;
            const i = sensOpenSlot, s = sensSlot(i), ty = sensTypeOf(s.t);
            let m = '';
            if (s.a) {
                if (!s.t) m = window.t('sens_need_type', 'Choose a type before activating this slot.');
                else for (let k = 0; k < ty.pins.length; k++) {
                    if (s.p[k] > SENS.gmax) { m = window.t('sens_need_pin', 'Assign a GPIO to pin') + ' ' + k + ' (' + ty.pins[k] + ').'; break; }
                }
                /* Cross-slot collision, same rule commit_all applies. */
                if (!m) for (let k = 0; k < ty.pins.length; k++) {
                    for (let j = 0; j < SENS.nslot && !m; j++) {
                        if (j === i) continue;
                        const o = sensSlot(j);
                        if (o.a && (o.p || []).indexOf(s.p[k]) >= 0)
                            m = 'GP' + s.p[k] + ' ' + window.t('sens_taken', 'is already used by slot') + ' ' + j + '.';
                    }
                    if (m) break;
                }
            }
            el.textContent = m;
            el.style.display = m ? '' : 'none';
            /* The calibration panel has its own warning line so a slot-config
               problem and a point problem can be visible at the same time. */
            const cw = SE('se_calwarn');
            if (cw) {
                const cm = calWarnMsg();
                cw.textContent = cm;
                cw.style.display = cm ? '' : 'none';
            }
        }

        function sensClear() {
            if (!confirm(window.t('sens_clear_confirm', 'Free this slot? Its type, GPIOs and identity are cleared.'))) return;
            sensPut({ i: sensOpenSlot, a: false, t: 0, p: [255, 255, 255, 255], hwId: '', name: '' });
            sensClose();
        }

        function sensRevert() {
            const i = sensOpenSlot;
            sensSet(sensStaged().filter(x => x.i !== i));
            /* The calibration section stages separately — drop this slot from
               it too, and forget the local point-editor state so the redraw
               below re-reads the server truth. Leaving it staged made Revert
               a lie for exactly the edits that rewrite flash. */
            const rest = calStaged().filter(x => x.slot !== i);
            Pending.setSection('calib', rest.length ? { sensors: rest } : {});
            Object.keys(calEdit).forEach(k => {
                if (k.indexOf(i + ':') === 0) { delete calEdit[k]; delete calDirty[k]; delete calMode[k]; }
            });
            /* Discarding a slot that only ever existed as a staged edit leaves
               nothing to edit — keeping the dialog open would show a blank slot
               that is no longer in the list behind it. */
            const s = sensSlot(i);
            if (!s.a && !s.t) sensClose(); else sensDrawEditor();
        }

        document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && sensOpenSlot >= 0) sensClose(); });
        document.addEventListener('DOMContentLoaded', () => { setTimeout(applyLang, 50); loadConfig(); loadSensors(); });
    </script>
</body>
</html>
)raw";


static const char NET_PAGE[] PROGMEM = R"raw(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate">
    <title>SIMUT - Network</title>
    <script src="/lang.js"></script>
    <link rel="stylesheet" href="/style.css">
    <style>


        /* Network Styles */
        .layout-grid { display: grid; grid-template-columns: 320px 1fr; gap: 25px; align-items: start; }
        @media(max-width: 900px) { .layout-grid { grid-template-columns: 1fr; } }
        h3 { color: var(--txt); border-bottom: 1px solid var(--border); padding-bottom: 10px; margin-top: 0; font-size: 1.1rem; }
        .grp { background: rgba(255,255,255,0.02); padding: 20px; border-radius: 8px; margin-bottom: 20px; border: 1px solid var(--border); }
        label { display: block; color: var(--sub); margin-bottom: 6px; font-size: 0.9rem; font-weight: 600; }
        .card input[type=text], .card input[type=password] { width: 100%; padding: 12px; background: var(--bg); border: 1px solid var(--border); color: var(--txt); border-radius: 6px; box-sizing: border-box; margin-bottom: 15px; font-size: 1rem; transition: 0.2s; }
        .card input:focus { border-color: var(--acc); outline: none; }
        .card button[type=submit] { width: 100%; padding: 14px; background: var(--acc); color: black; border: none; font-weight: bold; border-radius: 6px; cursor: pointer; font-size: 1rem; margin-top: 10px; transition: 0.2s; }
        .card button[type=submit]:hover { opacity: 0.9; transform: translateY(-1px); }
        .chk { display: flex; align-items: center; gap: 10px; margin-bottom: 20px; background: rgba(6, 182, 212, 0.05); padding: 15px; border-radius: 6px; border: 1px solid var(--acc); }
        .chk input[type=checkbox] { width: 20px; height: 20px; accent-color: var(--acc); cursor: pointer; }
        .cfg-tg { display: flex; align-items: center; gap: 12px; padding: 8px 0; cursor: pointer; }
        /* Esconde campos estáticos quando DHCP/DNS auto está ON */
        .grp:has(#dhcp:checked) #static_fields { display: none; }
        .grp:has(#dns_auto:checked) #dns_fields { display: none; }
        .row { display: flex; gap: 20px; }
        .col { flex: 1; }
        @media(max-width: 600px) { .row { flex-direction: column; gap: 0; } }
        .net-stat { margin-bottom: 15px; padding-bottom: 15px; border-bottom: 1px dashed var(--border); }
        .net-stat:last-child { border-bottom: none; margin-bottom: 0; padding-bottom: 0; }
        .net-stat .lbl { font-size: 0.8rem; color: var(--sub); text-transform: uppercase; font-weight: bold; }
        .net-stat .val { font-size: 1.1rem; color: var(--txt); font-family: monospace; margin-top: 4px; }
    </style>
    <script>
        /* window.t/applyLang/setLang/showToast/fetchSafe vem de /lang.js */

        /* U24 Phase D: Pending + commitAll centralizados em /lang.js. */
    </script>
</head>
<body>
    <script>installTopbar('nav_net', 'Network')</script>

    <div class="container">
        <div class="layout-grid">
            <div class="side-content">
                <div class="card">
                    <h3 data-i18n="net_curr">Current Connection Status</h3>
                    <div class="net-stat"><div class="lbl" data-i18n="net_stat">Status</div><div class="val" id="lbl_stat" style="color:var(--acc);">--</div></div>
                    <div class="net-stat"><div class="lbl" data-i18n="net_ip">IP Address</div><div class="val" id="lbl_ip">--</div></div>
                    <div class="net-stat"><div class="lbl" data-i18n="net_mask">Subnet Mask</div><div class="val" id="lbl_mask">--</div></div>
                    <div class="net-stat"><div class="lbl" data-i18n="net_gw">Gateway</div><div class="val" id="lbl_gw">--</div></div>
                    <div class="net-stat"><div class="lbl" data-i18n="net_dns">DNS Server</div><div class="val" id="lbl_dns">--</div></div>
                    <div class="net-stat"><div class="lbl" data-i18n="net_mac">MAC Address</div><div class="val" id="lbl_mac">--</div></div>
                </div>
            </div>

            <div class="main-content">
                <div class="card">
                    <h2 class="page-title" data-i18n="net_cfg">Network Configuration</h2>
                    <form id="netForm" onsubmit="event.preventDefault()">

                        <h3 data-i18n="net_wifi">Wireless Network (Wi-Fi)</h3>
                        <div class="grp">
                            <label data-i18n="net_ssid">SSID (Network Name)</label>
                            <input type="text" id="ssid" name="ssid" maxlength="31" required>

                            <label data-i18n="net_pass">Password (Leave empty to keep current)</label>
                            <input type="password" id="pass" name="pass" maxlength="31">
                        </div>

                        <h3 data-i18n="net_ipv4">IPv4 Configuration</h3>
                        <div class="grp">
                            <label class="cfg-tg">
                                <span class="toggle"><input type="checkbox" id="dhcp" name="use_dhcp" value="1" onchange="toggleIpFields()"><span class="slider"></span></span>
                                <span data-i18n="net_dhcp">Obtain IP automatically (DHCP)</span>
                            </label>

                            <div id="static_fields">
                                <div class="row">
                                    <div class="col">
                                        <label data-i18n="net_sip">Static IP Address</label>
                                        <input type="text" id="ip" name="ip" maxlength="15" placeholder="192.168.1.100">
                                    </div>
                                    <div class="col">
                                        <label data-i18n="net_mask">Subnet Mask</label>
                                        <input type="text" id="mask" name="mask" maxlength="15" placeholder="255.255.255.0">
                                    </div>
                                </div>
                                <div class="row">
                                    <div class="col">
                                        <label data-i18n="net_gw">Gateway</label>
                                        <input type="text" id="gw" name="gw" maxlength="15" placeholder="192.168.1.1">
                                    </div>
                                    <div class="col"></div>
                                </div>
                            </div>
                        </div>

                        <h3 data-i18n="net_dns_title">DNS Configuration</h3>
                        <div class="grp">
                            <label class="cfg-tg">
                                <span class="toggle"><input type="checkbox" id="dns_auto" name="dns_auto" value="1" onchange="toggleDnsFields()"><span class="slider"></span></span>
                                <span data-i18n="net_dns_auto">Obtain DNS automatically (DHCP)</span>
                            </label>
                            <div id="dns_fields">
                                <div class="row">
                                    <div class="col">
                                        <label data-i18n="net_dns1">Primary DNS</label>
                                        <input type="text" id="dns1" name="dns1" maxlength="15" placeholder="8.8.8.8">
                                    </div>
                                    <div class="col">
                                        <label data-i18n="net_dns2">Secondary DNS</label>
                                        <input type="text" id="dns2" name="dns2" maxlength="15" placeholder="8.8.4.4">
                                    </div>
                                </div>
                            </div>
                        </div>

                        <h3 data-i18n="net_ntp_title">NTP Time Server</h3>
                        <div class="grp">
                            <div id="ntp_disabled_hint" style="display:none;margin-bottom:8px;padding:8px;background:rgba(255,180,0,0.12);border-left:3px solid #f59e0b;border-radius:3px;font-size:0.9em">
                                <span data-i18n="net_ntp_disabled">NTP is disabled.</span>
                                <a href="/config" style="color:var(--acc);text-decoration:underline" data-i18n="net_ntp_goto_cfg">Enable in System Config &rarr;</a>
                            </div>
                            <label data-i18n="net_ntp_lbl">Server Address</label>
                            <input type="text" id="ntp_server" name="ntp_server" maxlength="31" placeholder="pool.ntp.org">
                            <div class="c-sub" style="margin-top:4px;font-size:0.8em;color:var(--sub)" data-i18n="net_ntp_hint">Leave empty to use default (pool.ntp.org)</div>
                        </div>

                        <h3 data-i18n="net_web_title">Web Server</h3>
                        <div class="grp">
                            <label data-i18n="net_web_port">HTTP Port</label>
                            <input type="text" id="web_port" name="web_port" maxlength="5" inputmode="numeric" placeholder="80">
                            <div class="c-sub" style="margin-top:4px;font-size:0.8em;color:var(--sub)" data-i18n="net_web_port_hint">Default: 80. After saving, browser auto-redirects to new port.</div>
                        </div>

                        <!-- U24 Phase C: save button removido. Use "Salvar e Reiniciar" no topbar. -->
                    </form>
                </div>
            </div>
        </div>
    </div>

    <script>
        function _toggleGroup(containerId, disabled) {
            let sf = document.getElementById(containerId);
            let inputs = sf.querySelectorAll('input');
            if(disabled) {
                sf.style.opacity = '0.3';
                inputs.forEach(i => { i.readOnly = true; i.style.pointerEvents = 'none'; });
            } else {
                sf.style.opacity = '1';
                inputs.forEach(i => { i.readOnly = false; i.style.pointerEvents = 'auto'; });
            }
        }
        function toggleIpFields()  { _toggleGroup('static_fields', document.getElementById('dhcp').checked); }
        function toggleDnsFields() { _toggleGroup('dns_fields',    document.getElementById('dns_auto').checked); }

        async function loadNet() {
            try {
                let res = await fetchSafe('/api/network'); let data = await res.json();
                if(data.error) return;

                document.getElementById('lbl_ip').innerText = data.ip;
                document.getElementById('lbl_mask').innerText = data.mask;
                document.getElementById('lbl_gw').innerText = data.gw;
                document.getElementById('lbl_dns').innerText = data.dns;
                document.getElementById('lbl_mac').innerText = data.mac;

                let s = document.getElementById('lbl_stat');
                if(data.connected) { s.innerText = window.t('net_conn', 'Connected'); s.style.color = "var(--acc)"; }
                else { s.innerText = window.t('net_off', 'Disconnected'); s.style.color = "var(--dang)"; }

                /* U24 Phase C: aplica valores do flash; sobrepõe pendentes. */
                const p = Pending.getSection('net');
                const val = (k, def) => (p[k] !== undefined ? p[k] : def);
                const bool = (k, def) => (p[k] !== undefined) ? (p[k] !== '0') : !!def;
                document.getElementById('ssid').value = val('ssid', data.ssid || '');
                document.getElementById('dhcp').checked = bool('use_dhcp', data.use_dhcp);
                document.getElementById('ip').value = val('ip', data.static_ip || '');
                document.getElementById('mask').value = val('mask', data.static_mask || '');
                document.getElementById('gw').value = val('gw', data.static_gw || '');
                /* F-NET-TIME.3b: DNS em seção separada — independente de DHCP. */
                document.getElementById('dns_auto').checked = bool('dns_auto', data.dns_auto);
                document.getElementById('dns1').value = val('dns1', data.static_dns || '');
                document.getElementById('dns2').value = val('dns2', data.dns2 || '');
                document.getElementById('ntp_server').value = val('ntp_server', data.ntp_server || '');
                document.getElementById('web_port').value = val('web_port', data.web_port || 80);
                /* Hint visível se NTP está off em /config. */
                const hint = document.getElementById('ntp_disabled_hint');
                if (hint) hint.style.display = (data.ntp_enabled === false) ? '' : 'none';
                toggleIpFields();
                toggleDnsFields();
                wireNetPendingListeners();
            } catch(e) {}
        }

        function wireNetPendingListeners() {
            const form = document.getElementById('netForm');
            if (!form || form._pendingWired) return;
            form._pendingWired = true;
            const handler = (ev) => {
                const el = ev.target;
                if (!el.id) return;
                let v;
                if (el.type === 'checkbox') v = el.checked ? '1' : '0';
                else v = el.value;
                Pending.setField('net', el.id === 'dhcp' ? 'use_dhcp' : el.id, v);
            };
            form.addEventListener('input', handler);
            form.addEventListener('change', handler);
        }

        document.addEventListener('DOMContentLoaded', () => { setTimeout(applyLang, 50); loadNet(); });
    </script>
</body>
</html>
)raw";


static const char USR_PAGE[] PROGMEM = R"raw(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate">
    <title>SIMUT - Users</title>
    <script src="/lang.js"></script>
    <link rel="stylesheet" href="/style.css">
    <style>


        /* User Styles */
        table { width: 100%; border-collapse: collapse; margin-bottom: 30px; }
        th, td { padding: 14px; text-align: left; border-bottom: 1px solid var(--border); }
        th { color: var(--sub); font-size: 0.85rem; text-transform: uppercase; }
        .badge { background: #3f3f46; color: white; padding: 4px 8px; border-radius: 6px; font-size: 0.75rem; margin-right: 4px; display: inline-block; margin-bottom: 4px; }
        .badge.full { background: var(--acc); color: black; font-weight: bold; }
        .btn-action { background: #3f3f46; color: white; border: none; padding: 6px 12px; border-radius: 4px; cursor: pointer; transition: 0.2s; font-size: 0.85rem; font-weight:600;}
        .btn-action:hover { background: #52525b; }
        .btn-dang { background: transparent; border: 1px solid var(--dang); color: var(--dang); padding: 5px 12px; border-radius: 4px; cursor: pointer; transition: 0.2s; font-size: 0.85rem; font-weight:600;}
        .btn-dang:hover { background: var(--dang); color: white; }
        .frm-box { background: rgba(255,255,255,0.02); border: 1px solid var(--border); padding: 20px; border-radius: 8px; }
        .chk-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 15px; margin-bottom: 15px; }
        @media(max-width: 600px) { .chk-grid { grid-template-columns: 1fr; } }
        .frm-box input[type=text] { width: 100%; padding: 12px; background: var(--bg); border: 1px solid var(--border); color: var(--txt); border-radius: 6px; box-sizing: border-box; font-size:1rem; outline:none; transition:0.2s;}
        .frm-box input:focus { border-color:var(--acc); }
        .chk-lbl { display: flex; align-items: center; gap: 8px; color: var(--txt); cursor: pointer; font-size: 0.9rem; }
        /* 22px: 16px era metade do minimo confortavel de toque, e sao 10 destes
           empilhados numa coluna so no celular. */
        .chk-lbl input[type=checkbox] { width: 22px; height: 22px; accent-color: var(--acc); cursor: pointer; }
        @media(max-width: 640px) { .chk-lbl { padding: 8px 0; } }
        .frm-box button[type=submit] { width: 100%; padding: 14px; background: var(--acc); color: black; font-weight: bold; border: none; border-radius: 6px; cursor: pointer; font-size:1rem; transition:0.2s;}
        .frm-box button[type=submit]:hover { opacity:0.9; }
        #commit-btn { background: #16a34a; color: #fff; border: none; padding: 7px 14px; border-radius: 6px; font-weight: 700; font-size: 0.82rem; cursor: pointer; display: none; }
        #commit-btn:hover { background: #15803d; }
        #commit-btn:disabled { opacity: 0.6; cursor: wait; }
    </style>
    <script>
        /* window.t/applyLang/setLang/showToast/fetchSafe vem de /lang.js */

        /* U24 Phase D: Pending + commitAll centralizados em /lang.js. */
    </script>
</head>
<body>
    <script>installTopbar('nav_usr', 'Users')</script>

    <div class="container">
        <div class="card" style="padding: 30px;">
            <h2 class="page-title" data-i18n="usr_mgt">Access Management</h2>
            <div style="overflow-x:auto;">
                <table>
                    <thead>
                        <tr>
                            <th style="width: 40px;">#</th>
                            <th data-i18n="usr_usr">User</th>
                            <th data-i18n="usr_perm">Permissions</th>
                            <th style="width: 140px; text-align: center;" data-i18n="usr_act">Actions</th>
                        </tr>
                    </thead>
                    <tbody id="usrBody">
                        <tr><td colspan="4" style="text-align:center; color:var(--sub);">Loading...</td></tr>
                    </tbody>
                </table>
            </div>

            <h3 style="color:var(--txt); border-bottom: 1px solid var(--border); padding-bottom:10px; margin-top:20px;" data-i18n="usr_add">Add New User</h3>
            <form class="frm-box" onsubmit="addUser(event)">
                <input type="text" id="u_name" name="u_name" placeholder="Username" data-i18n="usr_name" required maxlength="15">
                <div class="chk-grid">
                    <label class="chk-lbl"><input type="checkbox" name="p_dash" value="1"> <span data-i18n="usr_pdash">Dashboard (Real Time)</span></label>
                    <label class="chk-lbl"><input type="checkbox" name="p_hist" value="1"> <span data-i18n="usr_phist">History & Graphs</span></label>
                    <label class="chk-lbl"><input type="checkbox" name="p_logs" value="1"> <span data-i18n="usr_plog">System Logs</span></label>
                    <label class="chk-lbl"><input type="checkbox" name="p_sys" value="1"> <span data-i18n="usr_psys">System Config</span></label>
                    <label class="chk-lbl"><input type="checkbox" name="p_net" value="1"> <span data-i18n="usr_pnet">Network Config</span></label>
                    <label class="chk-lbl"><input type="checkbox" name="p_fread" value="1"> <span data-i18n="usr_pfr">Files (Download)</span></label>
                    <label class="chk-lbl"><input type="checkbox" name="p_fupl" value="1"> <span data-i18n="usr_pfu">Files (Upload)</span></label>
                    <label class="chk-lbl"><input type="checkbox" name="p_fdel" value="1"> <span data-i18n="usr_pfd">Files (Delete)</span></label>
                    <label class="chk-lbl"><input type="checkbox" name="p_usr" value="1"> <span data-i18n="usr_pusr">User Management</span></label>
                    <label class="chk-lbl"><input type="checkbox" name="p_calib" value="1"> <span data-i18n="usr_pcal">Sensor Calibration</span></label>
                </div>
                <button type="submit" id="btnUser" data-i18n="usr_btn">Create User</button>
                <p style="font-size:0.8rem; color:var(--sub); margin-top:15px; text-align:center;" data-i18n="usr_warn">
                    * A one-time password is shown after Save &amp; Restart. Copy it — it is displayed only once, and the user must change it on first login.
                </p>
            </form>
        </div>
    </div>

    <script>
        /* U24 Phase B: ações de usuário viram pending actions (add/del/reset).
         * loadUsers renderiza estado do server + overlay dos pendings como
         * linhas novas (add), strikethrough (del) ou badge (reset). */
        function renderPermsBadges(perms, isSuper) {
            if (isSuper) return `<span class="badge full" data-i18n="usr_sup">${window.t('usr_sup','Super Admin')}</span>`;
            let arr = [];
            const map = [[1,'usr_pdash','Dashboard'],[2,'usr_phist','History'],[4,'usr_plog','Logs'],[8,'usr_psys','Sys Config'],[16,'usr_pnet','Net Config'],[32,'usr_pfr','Files Read'],[64,'usr_pfu','Files Up'],[128,'usr_pfd','Files Del'],[256,'usr_pusr','Users'],[512,'usr_pcal','Calib']];
            map.forEach(([bit, key, def]) => { if (perms & bit) arr.push(`<span class="badge full" data-i18n="${key}">${window.t(key, def)}</span>`); });
            return arr.join('');
        }

        async function loadUsers() {
            try {
                let res = await fetchSafe('/api/users');
                let data = await res.json();
                const pending = (Pending.getSection('users') || {}).actions || [];
                const pendingDels = new Set(pending.filter(a => a.type === 'del').map(a => a.id));
                const pendingRsts = new Set(pending.filter(a => a.type === 'reset').map(a => a.id));
                const pendingAdds = pending.filter(a => a.type === 'add');

                let tbody = document.getElementById('usrBody');
                let html = '';
                /* Usuários existentes do server */
                data.forEach(u => {
                    const isSuper = (u.id === 0);
                    const isDel = pendingDels.has(u.id);
                    const isRst = pendingRsts.has(u.id);
                    const rowCls = isDel ? 'pending-del' : '';
                    let actions;
                    if (isSuper) {
                        actions = `<span class="badge" data-i18n="usr_prot">${window.t('usr_prot','Protected')}</span>`;
                    } else if (isDel) {
                        actions = `<span class="badge pending">${window.t('usr_pend_del','Pending: Delete')}</span>`;
                    } else {
                        let rstBtn = isRst
                            ? `<span class="badge pending">${window.t('usr_pend_rst','Pending: Reset')}</span>`
                            : `<button class="btn-action" onclick="rstUsr(${u.id})" data-i18n="usr_rst">${window.t('usr_rst','Reset')}</button>`;
                        actions = `${rstBtn} <button class="btn-dang" onclick="delUsr(${u.id})" data-i18n="usr_del">${window.t('usr_del','Del')}</button>`;
                    }
                    html += `<tr class="${rowCls}"><td>${u.id}</td><td style="font-weight:bold;color:var(--txt)">${escHtml(u.name)}</td><td>${renderPermsBadges(u.perms, isSuper)}</td><td style="text-align:center; white-space:nowrap;">${actions}</td></tr>`;
                });
                /* Usuários pendentes de criação. a.name é digitado pelo próprio
                   admin, mas ainda vai para innerHTML — escapa por profundidade
                   (M-7); o servidor já restringe o charset do username. */
                pendingAdds.forEach((a, i) => {
                    html += `<tr class="pending-add"><td>—</td><td style="font-weight:bold;color:var(--txt)">${escHtml(a.name)} <span class="badge pending">${window.t('usr_pend_add','Pending: New')}</span></td><td>${renderPermsBadges(a.perms, false)}</td><td style="text-align:center; white-space:nowrap;"><button class="btn-dang" onclick="undoLastAdd()">↶</button></td></tr>`;
                });
                tbody.innerHTML = html;
                applyLang();
            } catch(e) {}
        }

        function addUser(e) {
            e.preventDefault();
            const name = document.getElementById('u_name').value.trim();
            if (!name) return;
            let perms = 0;
            const bits = {p_dash:1, p_hist:2, p_logs:4, p_sys:8, p_net:16, p_fread:32, p_fupl:64, p_fdel:128, p_usr:256, p_calib:512};
            document.querySelectorAll('#u_name').forEach(() => {});
            Object.keys(bits).forEach(k => { const el = document.querySelector(`input[name="${k}"]`); if (el && el.checked) perms |= bits[k]; });
            Pending.pushUserAction({ type: 'add', name: name, perms: perms });
            document.getElementById('u_name').value = '';
            document.querySelectorAll('.chk-grid input[type=checkbox]').forEach(c => c.checked = false);
            loadUsers();
        }

        function delUsr(id) {
            if (!confirm(window.t('usr_del_msg', 'Delete this user? It will be applied when you click Save & Restart.'))) return;
            Pending.pushUserAction({ type: 'del', id: id });
            loadUsers();
        }

        function rstUsr(id) {
            if (!confirm(window.t('usr_rst_msg', 'Force a password reset at next login? It will be applied when you click Save & Restart.'))) return;
            Pending.pushUserAction({ type: 'reset', id: id });
            loadUsers();
        }

        /* Undo da última ação (só do tipo 'add' via botão ↶; desfaz última
         * ação qualquer). */
        function undoLastAdd() {
            Pending.popUserAction();
            loadUsers();
        }

        window.onLangChange = function() { loadUsers(); };

        document.addEventListener('DOMContentLoaded', () => { setTimeout(applyLang, 50); loadUsers(); });
    </script>
</body>
</html>
)raw";


static const char FILE_PAGE[] PROGMEM = R"raw(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate">
    <title>SIMUT - Files</title>
    <script src="/lang.js"></script>
    <link rel="stylesheet" href="/style.css">
    <style>
        h2.page-title { margin-bottom: 0; }


        /* Files Styles */
        .fm-toolbar { display:flex; justify-content:space-between; align-items:center; margin-bottom:16px; flex-wrap:wrap; gap:12px; }
        .fm-actions { display:flex; gap:8px; flex-wrap:wrap; align-items:center; }
        /* Uniform size. The six buttons sized themselves to their labels, so the
           row came out ragged — and differently ragged per language ("Download"
           vs "Baixar" vs "Descargar"). min-width is set from the longest label
           across the three packs ("Upload Here"), so no translation blows past
           it and the row stays even.
           inline-flex + gap also drops the reliance on the literal space in the
           markup to separate the emoji from the label. Safe against the four
           buttons that start hidden: the JS reveals them with display = '',
           which clears the inline value instead of writing one. */
        .btn-fm { padding: 7px 14px; border-radius: 6px; font-size: 0.85rem; font-weight: 600; cursor: pointer; transition: 0.2s; line-height: 1.2;
                  display: inline-flex; align-items: center; justify-content: center; gap: 6px;
                  min-width: 140px; white-space: nowrap; }
        /* Below 640px a 140px floor plus the toolbar padding leaves room for two
           per row; letting them share the width evenly beats a ragged wrap with
           one orphan button on the last line. */
        @media (max-width: 640px) {
          .fm-actions { width: 100%; }
          .btn-fm { flex: 1 1 calc(50% - 4px); min-width: 0; }
        }
        .btn-fm-pri { background: var(--acc); color: #000; border: 1px solid var(--acc); }
        .btn-fm-pri:hover { opacity: 0.9; transform: translateY(-1px); }
        .btn-fm-out { background: transparent; border: 1px solid var(--acc); color: var(--acc); }
        .btn-fm-out:hover { background: var(--acc); color: #000; }
        .btn-fm-dang { background: transparent; border: 1px solid var(--dang); color: var(--dang); }
        .btn-fm-dang:hover { background: var(--dang); color: #fff; }
        .f-chk { width: 18px; height: 18px; accent-color: var(--acc); cursor:pointer; }
        .breadcrumb { display:flex; align-items:center; gap:4px; margin-bottom:16px; padding:10px 14px; background:var(--bg); border:1px solid var(--border); border-radius:6px; font-size:0.9rem; flex-wrap:wrap; }
        .breadcrumb a { color:var(--acc); text-decoration:none; font-weight:600; cursor:pointer; transition:0.15s; }
        .breadcrumb a:hover { text-decoration:underline; }
        .breadcrumb span.sep { color:var(--sub); margin:0 2px; user-select:none; }
        .breadcrumb span.current { color:var(--txt); font-weight:700; }
        table { width: 100%; border-collapse: collapse; }
        th, td { padding: 12px 14px; text-align: left; border-bottom: 1px solid var(--border); }
        th { color: var(--sub); font-size: 0.8rem; text-transform: uppercase; letter-spacing:0.5px; }
        .fm-row { cursor:default; transition:background 0.15s; }
        .fm-row:hover { background: rgba(255,255,255,0.02); }
        .fm-row-dir { cursor:pointer; }
        .fm-row-dir:hover { background: rgba(6,182,212,0.06); }
        .fm-icon { margin-right:8px; font-size:1.1rem; vertical-align:middle; }
        .fm-name { color:var(--txt); font-weight:500; text-decoration:none; }
        .fm-name:hover { text-decoration:underline; }
        .fm-name-dir { color:var(--acc); font-weight:700; }
        .fm-size { font-family:monospace; color:var(--sub); font-size:0.85rem; }
        .fm-empty, .fm-loading { text-align:center; padding:30px; color:var(--sub); }
    </style>
    <script>
        /* window.t/applyLang/setLang/showToast/fetchSafe vem de /lang.js */
    </script>
</head>
<body>
    <script>installTopbar('nav_file', 'Files')</script>

    <div class="container">
        <div class="card">
            <div class="fm-toolbar">
                <h2 class="page-title" data-i18n="fil_title">Flash Filesystem</h2>
                <div class="fm-actions">
                    <button class="btn-fm btn-fm-out" onclick="fmDownload()">&#x2B07;&#xFE0F; <span data-i18n="fil_down">Download</span></button>
                    <button class="btn-fm btn-fm-out" onclick="fmBackup()" title="Download all files as a single .bkp" id="btnBackup" style="display:none">&#x1F4BE; <span data-i18n="fil_backup">Backup</span></button>
                    <button class="btn-fm btn-fm-out" onclick="fmRestore()" title="Upload a .bkp to restore" id="btnRestore" style="display:none">&#x267B;&#xFE0F; <span data-i18n="fil_restore">Restore</span></button>
                    <input type="file" id="restoreFile" accept=".bkp" style="display:none" onchange="doRestore()">
                    <button class="btn-fm btn-fm-out" onclick="fmFirmware()" title="Send new firmware (.bin) — OTA update" id="btnFw" style="display:none">&#x1F4BB; <span data-i18n="fil_fw">Firmware</span></button>
                    <input type="file" id="fwFile" accept=".bin" style="display:none" onchange="doFirmware()">
                    <button class="btn-fm btn-fm-dang" id="btnDel" style="display:none" onclick="fmDelete()">&#x1F5D1;&#xFE0F; <span data-i18n="fil_del">Delete</span></button>
                    <button class="btn-fm btn-fm-pri" id="btnUpload" style="display:none" onclick="fmUploadClick()">&#x1F4E4; <span data-i18n="fil_uphere">Upload Here</span></button>
                    <form id="upForm" method="POST" action="/api/upload" enctype="multipart/form-data" style="display:none;">
                        <input type="hidden" name="uploadDir" id="uploadDir" value="/">
                        <input type="file" name="uploadFile" id="uploadFile" multiple onchange="doUpload()">
                    </form>
                </div>
            </div>
            <div id="breadcrumb" class="breadcrumb"></div>
            <div style="overflow-x:auto;">
                <table>
                    <thead><tr>
                        <th style="width:40px"><input type="checkbox" class="f-chk" onclick="fmToggleAll(this)"></th>
                        <th data-i18n="fil_name">Name</th>
                        <th style="width:72px" data-i18n="fil_sz">Size</th>
                    </tr></thead>
                    <tbody id="fileBody">
                        <tr><td colspan="3" class="fm-loading" data-i18n="fil_loading">Loading...</td></tr>
                    </tbody>
                </table>
            </div>
        </div>
    </div>

    <script>
        let currentDir = '/';
        let permsVal = 0;

        async function fetchPerms() {
            try { let r = await fetchSafe('/api/perms'); let d = await r.json(); permsVal = d.perms; } catch(e) { }
            if (permsVal & 128) document.getElementById('btnDel').style.display = '';
            if (permsVal & 64) { document.getElementById('btnUpload').style.display = ''; }
            /* Backup, Restore and Firmware are admin-only (perms == 0xFFFF).
             * Backup dumps the whole filesystem including /config/system.bin
             * (secrets + password hashes) and the server now gates it at full
             * admin (finding A-4); Restore always did server-side but the button
             * used to appear for uploaders, so a non-admin saw it and hit a 403.
             * All three destructive/sensitive whole-FS ops now match their gate. */
            if (permsVal === 65535) {
                document.getElementById('btnBackup').style.display = '';
                document.getElementById('btnRestore').style.display = '';
                document.getElementById('btnFw').style.display = '';
            }
        }

        function fmFormatSize(bytes) { if (bytes === 0) return '—'; if (bytes < 1024) return bytes + ' B'; if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB'; return (bytes / 1048576).toFixed(2) + ' MB'; }

        function fmBuildBreadcrumb(path) {
            /* Segments come from server folder names, so both the link target
               (data-nav, read via getAttribute — never executed) and the label
               (escHtml) are escaped. Was inline onclick with the raw segment
               interpolated into a JS string — a quote in a folder name broke
               out (M-7). The href/data-nav navigation runs through the same
               delegated [data-nav] handler as the file rows. */
            let bc = document.getElementById('breadcrumb'); let parts = path.split('/').filter(p => p.length > 0); let html = '<a data-nav="/">&#x1F4BE;</a>'; let accum = '';
            for (let i = 0; i < parts.length; i++) {
                accum += '/' + parts[i]; html += '<span class="sep">/</span>';
                if (i === parts.length - 1) html += '<span class="current">' + escHtml(parts[i]) + '</span>'; else html += '<a data-nav="' + escAttr(accum) + '">' + escHtml(parts[i]) + '</a>';
            }
            bc.innerHTML = html;
        }

        async function fmNavigate(dir) {
            currentDir = dir; document.getElementById('uploadDir').value = dir; fmBuildBreadcrumb(dir);
            let tbody = document.getElementById('fileBody'); tbody.innerHTML = `<tr><td colspan="3" class="fm-loading">${window.t('fil_loading','Loading...')}</td></tr>`;
            try {
                let res = await fetchSafe('/api/ls?dir=' + encodeURIComponent(dir)); let data = await res.json();
                if (data.error) { tbody.innerHTML = `<tr><td colspan="3" class="fm-empty" style="color:var(--dang)">${data.error}</td></tr>`; return; }
                let entries = data.entries || [];
                entries.sort((a, b) => { if (a.t !== b.t) return a.t === 'd' ? -1 : 1; return a.n.localeCompare(b.n); });
                let html = '';
                if (dir !== '/') { let parent = dir.substring(0, dir.lastIndexOf('/')); if (parent === '') parent = '/'; html += `<tr class="fm-row fm-row-dir" data-nav="${escAttr(parent)}"><td></td><td><span class="fm-icon">&#x2B06;&#xFE0F;</span><span class="fm-name-dir">..</span></td><td class="fm-size">${window.t('fil_parent','Parent')}</td></tr>`; }
                if (entries.length === 0 && dir === '/') { html += `<tr><td colspan="3" class="fm-empty">${window.t('fil_empty','Empty filesystem')}</td></tr>`; }
                else {
                    for (let e of entries) {
                        if (e.t === 'd') { let fullPath = (dir === '/' ? '/' : dir + '/') + e.n; html += `<tr class="fm-row fm-row-dir" data-nav="${escAttr(fullPath)}"><td></td><td><span class="fm-icon">&#x1F4C1;</span><span class="fm-name-dir">${escHtml(e.n)}/</span></td><td class="fm-size">${window.t('fil_folder','Folder')}</td></tr>`; }
                        else {
                            let fullPath = (dir === '/' ? '/' : dir + '/') + e.n;
                            /* e.p = protected by the firmware. No checkbox at all, so it
                               cannot be selected and fmDelete never sees it. The server
                               refuses it too — this is the visible half. */
                            let cell = e.p ? `<span class="fm-icon" title="${window.t('fil_protected','Protected file')}">&#x1F512;</span>`
                                           : `<input type="checkbox" class="f-chk item-chk" value="${escAttr(fullPath)}">`;
                            /* The name is a download link for EVERY file. Reading used to
                               require ticking the box and pressing Download, which left a
                               protected file — the one with no box — impossible to open at
                               all. Folders were already clickable; files now match. */
                            let href = '/download?file=' + encodeURIComponent(fullPath);
                            html += `<tr class="fm-row"><td>${cell}</td><td><span class="fm-icon">&#x1F4C4;</span><a class="fm-name" href="${href}">${escHtml(e.n)}</a></td><td class="fm-size">${fmFormatSize(e.s)}</td></tr>`;
                        }
                    }
                }
                tbody.innerHTML = html;
            } catch (err) { tbody.innerHTML = `<tr><td colspan="3" class="fm-empty" style="color:var(--dang)">Error: ${err.message}</td></tr>`; }
        }

        function fmToggleAll(src) { document.querySelectorAll('.item-chk').forEach(c => c.checked = src.checked); }
        function fmUploadClick() { document.getElementById('uploadFile').click(); }
        async function doUpload() {
            let fData = new FormData(document.getElementById('upForm'));
            try { await fetchSafe('/api/upload', { method: 'POST', body: fData, timeout: 60000 }); fmNavigate(currentDir); showToast(window.t('fil_uploaded','Upload complete.'), 'ok'); } catch(e) { showToast(window.t('fil_up_err','Upload error'), 'err'); }
        }
        async function fmDelete() {
            let sel = document.querySelectorAll('.item-chk:checked'); if (sel.length === 0) { showToast(window.t('fil_sel_del', 'Select files to delete.'), 'warn'); return; }
            let msg = window.t('fil_conf_del', 'Are you sure you want to delete N files?').replace('N', sel.length); if (!confirm(msg)) return;
            let busy = false, failed = 0, done = 0;
            try {
                for (let i = 0; i < sel.length; i++) { let r = await fetchSafe('/api/delete?file=' + encodeURIComponent(sel[i].value), {method:'POST'}); if (r.status === 503) { busy = true; break; } if (r.ok) done++; else failed++; }
            } catch(e) { showToast(window.t('net_conn_err','Connection error.'), 'err'); fmNavigate(currentDir); return; }
            if (busy) showToast(window.t('display_busy','Display in use. Try again shortly.'), 'warn');
            else if (failed > 0) showToast(window.t('fil_del_err','Some files could not be deleted.'), 'err');
            else if (done > 0) showToast(window.t('fil_deleted','Files deleted.'), 'ok');
            fmNavigate(currentDir);
        }
        async function fmDownload() {
            let sel = document.querySelectorAll('.item-chk:checked'); if (sel.length === 0) { showToast(window.t('fil_sel_down', 'Select files.'), 'warn'); return; }
            if (sel.length > 1) { let msg = window.t('fil_conf_down', 'Download N files?').replace('N', sel.length); if (!confirm(msg)) return; }
            for (let i = 0; i < sel.length; i++) {
                let link = document.createElement('a'); link.href = '/download?file=' + encodeURIComponent(sel[i].value); link.setAttribute('download', '');
                document.body.appendChild(link); link.click(); document.body.removeChild(link); await new Promise(r => setTimeout(r, 800));
            }
        }
        function fmBackup() {
            let link = document.createElement('a'); link.href = '/api/backup'; link.setAttribute('download', '');
            document.body.appendChild(link); link.click(); document.body.removeChild(link);
            showToast(window.t('fil_backup_started','Backup download started.'), 'ok');
        }
        const RST_MSG = {0:'OK',1:'magic invalid',2:'unsupported schema',3:'header CRC mismatch',4:'payload truncated',5:'payload CRC mismatch',6:'chip ID mismatch (backup is from another device)',7:'invalid path',8:'path too long',9:'I/O error',10:'internal error'};
        function fmRestore() { document.getElementById('restoreFile').click(); }
        async function doRestore() {
            let inp = document.getElementById('restoreFile');
            if (!inp.files.length) return;
            let file = inp.files[0]; inp.value = '';
            try {
                showToast(window.t('fil_rst_val','Step 1/3: Validating backup (CRC + chip ID)...'), 'ok');
                let fd = new FormData(); fd.append('bkp', file);
                let r = await fetch('/api/restore?op=validate', { method:'POST', body: fd });
                let v = await r.json();
                if (v.st !== 0) { showToast(window.t('fil_rst_val_fail','Validation failed: ') + (RST_MSG[v.st] || ('st='+v.st)), 'err'); return; }
                let msg = window.t('fil_rst_confirm','Backup valid — N files (X bytes).\n\nThis OVERWRITES current device files (history, configs, sensors, themes).\nWi-Fi config and admin password from the backup will be restored.\n\nProceed with the restore?').replace('N', v.fc).replace('X', v.psz);
                if (!confirm(msg)) return;
                showToast(window.t('fil_rst_app','Step 2/3: Applying restore (may take ~15 s)...'), 'ok');
                let fd2 = new FormData(); fd2.append('bkp', file);
                let applied = false;
                try {
                    let r2 = await fetch('/api/restore?op=apply', { method:'POST', body: fd2 });
                    let a = await r2.json();
                    if (a.st === 0) applied = true;
                    else { showToast(window.t('fil_rst_app_fail','Apply failed: ') + (RST_MSG[a.st] || ('st='+a.st)), 'err'); return; }
                } catch(e) { applied = true; }
                if (!applied) return;
                showToast(window.t('fil_rst_reb','Step 3/3: Rebooting device (~25 s)...'), 'ok');
                for (let w = 0; w < 30; w++) {
                    await new Promise(r => setTimeout(r, 3000));
                    try { let p = await fetch('/api/login_init?_=' + Date.now()); if (p.ok) { location.href = '/'; return; } } catch(e) {}
                }
                showToast(window.t('fil_rst_off','Device offline after 90 s. Check connection and power-cycle if needed.'), 'err');
            } catch(e) { showToast(window.t('net_conn_err','Connection error.'), 'err'); }
        }

        /* OTA Firmware Update — v4.5.0 flow:
         *   1. Backup .bkp (preserva user data) → download local
         *   2. Confirma apply
         *   3. Stage upload (~30s, erase on-demand sector-by-sector — TCP estável)
         *   4. Apply (~5s) → device reboota
         *   5. Boot ~25s (snapshot config preservada → admin pwd intacta)
         *   6. Restore manual do .bkp pra recuperar history/sensores
         */
        function fmFirmware() {
            if (!confirm(window.t('fil_fw_warn','Firmware OTA update.\n\nThe filesystem is REFORMATTED — the staging area shares the partition with it.\n\nOnly the configuration survives (captured before, restored after):\n  • Wi-Fi, admin password, telemetry\n  • Sensor mapping and alarm limits\n\nEverything else on the device is lost:\n  • Reading history (/history)\n  • Language packs (/lang)\n  • Custom themes (/themes)\n  • Touch calibration (/calib)\n  • Files in /web\n  • The event log\n\nA .bkp backup is downloaded automatically — restore it later to recover everything.\n\nProceed?'))) return;
            document.getElementById('fwFile').click();
        }
        async function doFirmware() {
            let f = document.getElementById('fwFile').files[0];
            document.getElementById('fwFile').value = '';
            if (!f || !/\.bin$/i.test(f.name)) { showToast(window.t('fil_fw_need_bin','Please select a .bin firmware file'),'err'); return; }
            showToast(window.t('fil_fw_bk','Step 1/4: Downloading .bkp backup...'), 'ok');
            let r, bk;
            try { r = await fetch('/api/backup'); if (!r.ok) throw 0; bk = await r.blob(); }
            catch(e) { showToast(window.t('fil_fw_bk_fail','Backup download failed. Try again.'),'err'); return; }
            let psz = +r.headers.get('X-Backup-PSize'), pcrc = +r.headers.get('X-Backup-PCrc');
            let dv = new DataView(await bk.arrayBuffer());
            if (dv.getUint32(0,true) !== 0x31504B42 || dv.getUint32(24,true) !== psz || dv.getUint32(28,true) !== pcrc) {
                showToast(window.t('fil_fw_bk_corrupt','Backup corrupted (CRC). Aborted.'),'err'); return;
            }
            let u = URL.createObjectURL(bk);
            let a = document.createElement('a'); a.href = u;
            a.download = 'simut_pre-ota_'+Date.now()+'.bkp';
            document.body.appendChild(a); a.click(); a.remove(); URL.revokeObjectURL(u);
            if (!confirm(window.t('fil_fw_apply','Backup saved to your computer.\n\nStart the update?\n  • Firmware upload: ~30 s\n  • Apply + reboot: ~25 s\n  • Full boot: ~25 s\n\nTotal: ~80 s. Do not power off the device during the process.'))) return;
            showToast(window.t('fil_fw_up','Step 2/4: Uploading firmware (~30 s)...'), 'ok');
            try {
                let fd = new FormData(); fd.append('file', f);
                let r2 = await fetch('/api/restore?op=stage&commit=1', {method:'POST', body:fd});
                let v = await r2.json();
                if (r2.status !== 200 || v.committed !== 1) {
                    showToast(window.t('fil_fw_stage_fail','Upload failed (validation v=')+v.v+'). Cancelled.', 'err');
                    return;
                }
                showToast(window.t('fil_fw_app','Step 3/4: Applying firmware...'), 'ok');
                /* The apply result used to be discarded — `await fetch(...)` with no
                   check, inside a `catch(e) {}` that swallowed everything. A refusal
                   (403 not full admin, 409 nothing committed) still showed "Step 4/4:
                   waiting for boot" and bounced to /login, so a device that never
                   updated looked exactly like one that did. That is the whole reason
                   "it is still on the old version" arrived with no error to go on. */
                let r3 = await fetch('/api/ota/apply', {method:'POST'});
                if (!r3.ok) {
                    let why = '';
                    try { why = (await r3.json()).error || ''; } catch(e) { try { why = await r3.text(); } catch(e2) {} }
                    showToast(window.t('fil_fw_apply_fail','Apply refused (HTTP ') + r3.status + '). ' + why, 'err', 12000);
                    return;
                }
                showToast(window.t('fil_fw_reb','Step 4/4: Waiting for boot (~25 s)...'), 'ok');
            } catch(e) {
                showToast(window.t('fil_fw_apply_err','Apply failed: ') + (e && e.message ? e.message : e), 'err', 12000);
                return;
            }
            setTimeout(() => location.href = '/login', 5000);
        }

        window.onLangChange = function() { fmNavigate(currentDir); };

        document.addEventListener('DOMContentLoaded', () => { setTimeout(applyLang, 50); fetchPerms().then(() => fmNavigate('/')); });
    </script>
</body>
</html>
)raw";


static const char ALARMS_PAGE[] PROGMEM = R"raw(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate">
    <title>SIMUT - Alarms & Sounds</title>
    <script src="/lang.js"></script>
    <link rel="stylesheet" href="/style.css">
    <style>
        .card { margin-bottom: 24px; }
        h3 { color: var(--txt); border-bottom: 1px solid var(--border); padding-bottom: 10px; margin-top: 30px; font-size: 1.1rem; }


        /* ── Sensor cards ───────────────────────────────────────────── */
        .sensor-card { background: rgba(255,255,255,0.02); border: 1px solid var(--border); border-radius: 10px; padding: 20px; margin-bottom: 16px; }
        .sensor-header { display: flex; align-items: center; gap: 12px; margin-bottom: 14px; }
        .sensor-header .sensor-name { flex: 1; font-weight: 700; font-size: 1.05rem; color: var(--txt); }
        .sensor-name { font-weight: 700; font-size: 1.05rem; color: var(--txt); }
        /* Limites de alarme só aparecem quando o toggle do card está ON */
        .sensor-card:has(.alm-active:not(:checked)) .limit-grid { display: none; }
        .sensor-card:has(.alm-active:not(:checked)) { opacity: 0.7; }
        .sensor-type { font-size: 0.8rem; color: var(--sub); background: rgba(255,255,255,0.05); padding: 3px 10px; border-radius: 12px; }
        .limit-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
        @media(max-width: 500px) { .limit-grid { grid-template-columns: 1fr; } }
        .limit-field label { display: block; color: var(--sub); margin-bottom: 4px; font-size: 0.82rem; font-weight: 600; }
        /* 16px: era 0.95rem = 15.2px, o ultimo campo do app que ainda disparava o
           zoom automatico do iOS no foco (e o iOS nao desfaz o zoom no blur). */
        .limit-field input { width: 100%; padding: 10px; background: var(--bg); border: 1px solid var(--border); color: var(--txt); border-radius: 6px; box-sizing: border-box; font-size: 16px; transition: border-color 0.2s; }
        .limit-field input:focus { border-color: var(--acc); outline: none; }
        /* .alm-tg removida — toggle agora vive no .sensor-header */
        /* Spinners de number removidos globalmente via LANG_JS */

        /* ── Sounds card ────────────────────────────────────────────── */
        .sound-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 14px 24px; }
        @media(max-width: 500px) { .sound-grid { grid-template-columns: 1fr; } }
        .sound-item { display: flex; align-items: center; gap: 12px; background: rgba(255,255,255,0.02); border: 1px solid var(--border); border-radius: 8px; padding: 12px 16px; }
        .sound-item > .sound-label { flex: 1; min-width: 0; }   /* label cresce — mel-group e toggle ficam alinhados a' direita */
        .sound-item > .mel-group { flex-shrink: 0; }
        .sound-label { font-weight: 600; font-size: 0.9rem; }
        /* .toggle / .slider movido p/ LANG_JS (global em todas as paginas) */
        .vol-row { display: flex; align-items: center; gap: 14px; margin-top: 16px; padding: 14px 16px; background: rgba(255,255,255,0.02); border: 1px solid var(--border); border-radius: 8px; }
        .vol-row .sound-label { min-width: 70px; }
        .vol-row input[type=range] { flex: 1; accent-color: var(--acc); cursor: pointer; }
        .vol-val { font-weight: 700; color: var(--acc); min-width: 42px; text-align: right; }
        /* Largura padronizada dos selects de som via wrapper .csel-mel */
        .csel-mel { min-width: 140px; }
        .btn-test { background: none; border: 1px solid var(--border); border-radius: 6px; color: var(--sub); cursor: pointer; font-size: 0.9rem; padding: 4px 8px; transition: 0.2s; line-height: 1; }
        .btn-test:hover { border-color: var(--acc); color: var(--acc); }
        .btn-test:active { transform: scale(0.92); }
        /* A linha somava 278px inegociaveis (csel-mel 140 + teste + toggle 44 +
           gaps + padding) num espaco de 272px, e sem flex-wrap nada descia de
           linha: o interruptor — o controle principal — era o que saia da tela.
           Rotulo toma a 1a linha inteira; os controles dividem a 2a. */
        @media(max-width: 640px) {
            .sound-item { flex-wrap: wrap; gap: 8px 10px; padding: 12px; }
            .sound-item > .sound-label { flex: 1 1 100%; }
            .sound-item > .mel-group { flex: 1 1 auto; min-width: 0; }
            .mel-group > .csel-mel { flex: 1; min-width: 0; }
            .btn-test { min-width: 44px; }
            /* flex:1 deixa min-width:auto, e o minimo automatico de um range e a
               largura intrinseca do controle (~129px) — ele se recusava a encolher */
            .vol-row { gap: 10px; padding: 12px; }
            .vol-row .sound-label { min-width: 0; }
            .vol-row input[type=range] { min-width: 0; }
        }
        .mel-group { display: flex; align-items: center; gap: 6px; }

        /* ── Botão salvar ───────────────────────────────────────────── */
        .btn-save { width: 100%; padding: 14px; background: var(--acc); color: #000; border: none; font-weight: bold; border-radius: 8px; cursor: pointer; font-size: 1rem; margin-top: 8px; transition: 0.2s; }
        .btn-save:hover { opacity: 0.9; transform: translateY(-1px); }
        .btn-save:disabled { background: #3f3f46; color: #a1a1aa; cursor: not-allowed; transform: none; }
        .empty-msg { text-align: center; color: var(--sub); padding: 40px 0; font-size: 1rem; }
    </style>
    <script>
        /* window.t/applyLang/setLang/showToast/fetchSafe vem de /lang.js */

        /* U24 Phase A.2 — Pending Changes Manager (duplicado de /config;
         * U24 Phase D: centralizado em /lang.js. */
    </script>
</head>
<body>
    <script>installTopbar('nav_alm', 'Alarms &amp; Sounds')</script>

    <div class="container">
        <!-- ═══════════ SEÇÃO: LIMITES DE ALARME POR SENSOR ═══════════ -->
        <div class="card">
            <h2 class="page-title" data-i18n="alm_title">Alarms & Sounds</h2>
            <h3 style="margin-top:0;" data-i18n="alm_limits">Alarm Limits</h3>
            <div id="sensor-list">
                <div class="empty-msg" data-i18n="alm_none">No sensors configured.</div>
            </div>
        </div>

        <!-- ═══════════ SEÇÃO: CONFIGURAÇÃO DE SONS ═══════════ -->
        <div class="card">
            <h3 style="margin-top:0;" data-i18n="alm_sounds">Sound Settings</h3>
            <div class="sound-grid">
                <div class="sound-item">
                    <span class="sound-label" data-i18n="alm_touch">Touch</span>
                    <div class="mel-group">
                        <select id="mel_touch" class="mel-sel">
                            <option value="0">Click</option>
                            <option value="1">Bubble</option>
                            <option value="2">Tick</option>
                            <option value="3">Snap</option>
                            <option value="4">Drop</option>
                            <option value="5">Chirp</option>
                        </select>
                        <button type="button" class="btn-test" onclick="testSound('touch')">&#9835;</button>
                    </div>
                    <label class="toggle"><input type="checkbox" id="snd_touch"><span class="slider"></span></label>
                </div>
                <div class="sound-item">
                    <span class="sound-label" data-i18n="alm_confirm">Confirm</span>
                    <div class="mel-group">
                        <select id="mel_confirm" class="mel-sel">
                            <option value="0" data-i18n="alm_mel_asc">Ascending</option>
                            <option value="1">Fanfare</option>
                            <option value="2">Chime</option>
                            <option value="3">Triumph</option>
                            <option value="4">Sparkle</option>
                            <option value="5">Resolve</option>
                        </select>
                        <button type="button" class="btn-test" onclick="testSound('confirm')">&#9835;</button>
                    </div>
                    <label class="toggle"><input type="checkbox" id="snd_confirm"><span class="slider"></span></label>
                </div>
                <div class="sound-item">
                    <span class="sound-label" data-i18n="alm_error">Error</span>
                    <div class="mel-group">
                        <select id="mel_error" class="mel-sel">
                            <option value="0" data-i18n="alm_mel_desc">Descending</option>
                            <option value="1">Buzz</option>
                            <option value="2">Low</option>
                            <option value="3">Harsh</option>
                            <option value="4">Decline</option>
                            <option value="5">Blip</option>
                        </select>
                        <button type="button" class="btn-test" onclick="testSound('error')">&#9835;</button>
                    </div>
                    <label class="toggle"><input type="checkbox" id="snd_error"><span class="slider"></span></label>
                </div>
                <div class="sound-item">
                    <span class="sound-label" data-i18n="alm_alarm">Alarm</span>
                    <div class="mel-group">
                        <select id="mel_alarm" class="mel-sel">
                            <option value="0">Dual Beep</option>
                            <option value="1" data-i18n="alm_mel_siren">Siren</option>
                            <option value="2">Rapid</option>
                            <option value="3">Pulse</option>
                            <option value="4">Escalate</option>
                            <option value="5">Staccato</option>
                        </select>
                        <button type="button" class="btn-test" onclick="testSound('alarm')">&#9835;</button>
                    </div>
                    <label class="toggle"><input type="checkbox" id="snd_alarm"><span class="slider"></span></label>
                </div>
                <div class="sound-item">
                    <span class="sound-label" data-i18n="alm_web">Web Sounds</span>
                    <label class="toggle"><input type="checkbox" id="snd_web"><span class="slider"></span></label>
                </div>
                <div class="sound-item">
                    <span class="sound-label" data-i18n="alm_attention">Attention</span>
                    <div class="mel-group">
                        <select id="mel_attention" class="mel-sel">
                            <option value="0">Notify</option>
                            <option value="1">Bell</option>
                            <option value="2">Pulse</option>
                            <option value="3">Chime Low</option>
                            <option value="4">Rise</option>
                            <option value="5">Soft Ding</option>
                        </select>
                        <button type="button" class="btn-test" onclick="testSound('attention')">&#9835;</button>
                    </div>
                    <label class="toggle"><input type="checkbox" id="snd_attention"><span class="slider"></span></label>
                </div>
                <div class="sound-item">
                    <span class="sound-label" data-i18n="alm_mute">Global Mute</span>
                    <label class="toggle"><input type="checkbox" id="snd_mute"><span class="slider"></span></label>
                </div>
            </div>
            <div class="vol-row">
                <span class="sound-label" data-i18n="alm_volume">System Volume</span>
                <input type="range" id="snd_volume" min="0" max="100" step="5" value="70">
                <span class="vol-val" id="vol-display">70%</span>
            </div>
            <div class="vol-row">
                <span class="sound-label" data-i18n="alm_alarm_vol">Alarm Volume</span>
                <input type="range" id="snd_alarm_volume" min="0" max="100" step="5" value="70">
                <span class="vol-val" id="alarm-vol-display">70%</span>
            </div>
            <!-- U24: Save button removido. Use "Salvar e Reiniciar" no topbar. -->
        </div>
    </div>

    <script>
        // ═══════════════════════════════════════════════════════════════
        // showToast vem de /lang.js (window.showToast)
        // ═══════════════════════════════════════════════════════════════
        // INICIALIZAÇÃO DE SESSÃO E PERMISSÕES (padrão SPA do SIMUT)
        // ═══════════════════════════════════════════════════════════════

        // ═══════════════════════════════════════════════════════════════
        // CARREGA DADOS DE ALARMES E SONS DO FIRMWARE
        // ═══════════════════════════════════════════════════════════════
        async function loadAlarms() {
            try {
                let r = await fetchSafe('/api/alarms');
                if (!r.ok) throw new Error('API error');
                let data = await r.json();

                // ── Popula cards de sensores ──────────────────────────
                let container = document.getElementById('sensor-list');
                let sensors = data.sensors || [];

                if (sensors.length === 0) {
                    container.innerHTML = '<div class="empty-msg" data-i18n="alm_none">No sensors configured.</div>';
                    applyLang();
                    return;
                }

                let html = '';
                for (let i = 0; i < sensors.length; i++) {
                    let s = sensors[i];
                    /* s.has_hum is still emitted but no longer read: which fields
                       this card shows comes from s.lim, which lists exactly the
                       quantities the part reports. */
                    /* Every entry is a real slot. The idx === -1 branch here
                       painted an "Ambient Sensor" card for a pseudo-sensor the
                       API never emitted. */
                    let nameLabel = escHtml(s.name);

                    html += '<div class="sensor-card" data-idx="' + s.idx + '">';
                    html += '  <div class="sensor-header">';
                    html += '    <label class="toggle" title="Alarm"><input type="checkbox" class="alm-active"' + (s.active ? ' checked' : '') + '><span class="slider"></span></label>';
                    html += '    <div class="sensor-name">' + nameLabel + '</div>';
                    html += '    <div class="sensor-type">' + escHtml(s.type) + ' &middot; SLOT ' + s.idx + '</div>';
                    html += '  </div>';
                    html += '  <div class="limit-grid">';
                    /* Two fields per quantity the part reports, from s.lim.
                       Was a temperature pair plus a humidity pair behind hasHum,
                       so a BMP280 offered a temperature band and no way to bound
                       its pressure. data-key stays the channel key, which is what
                       the commit handler parses out of the payload. */
                    const lim = s.lim || {};
                    Object.keys(lim).forEach(k => {
                        const pair = lim[k];
                        if (!Array.isArray(pair) || pair.length < 2) return;
                        const nm = window.t('ch_' + k, k);
                        html += '    <div class="limit-field"><label>' + escHtml(nm) + ' MIN</label>'
                             +  '<input type="number" step="0.1" class="alm-input" data-key="' + escHtml(k) + 'min"'
                             +  ' value="' + Number(pair[0]).toFixed(1) + '"></div>';
                        html += '    <div class="limit-field"><label>' + escHtml(nm) + ' MAX</label>'
                             +  '<input type="number" step="0.1" class="alm-input" data-key="' + escHtml(k) + 'max"'
                             +  ' value="' + Number(pair[1]).toFixed(1) + '"></div>';
                    });
                    html += '  </div>';
                    html += '</div>';
                }
                container.innerHTML = html;

                // ── Popula configuração de sons ──────────────────────
                let snd = data.sounds || {};
                document.getElementById('snd_touch').checked     = !!snd.touch;
                document.getElementById('snd_confirm').checked   = !!snd.confirm;
                document.getElementById('snd_error').checked     = !!snd.error;
                document.getElementById('snd_alarm').checked     = !!snd.alarm;
                document.getElementById('snd_web').checked       = !!snd.web;
                document.getElementById('snd_attention').checked = (snd.attention === undefined) ? true : !!snd.attention;
                document.getElementById('snd_mute').checked      = !!snd.mute;
                /* Mudo Global: ativar mute desliga outros; ativar outro desliga mute (v3.32.3 inclui attention) */
                {const M=document.getElementById('snd_mute'),O=['snd_touch','snd_confirm','snd_error','snd_alarm','snd_web','snd_attention'];if(!M.dataset.lk){M.dataset.lk='1';M.addEventListener('change',e=>{if(e.target.checked)O.forEach(i=>document.getElementById(i).checked=false)});O.forEach(i=>document.getElementById(i).addEventListener('change',e=>{if(e.target.checked)M.checked=false}))}}
                let vol = (snd.volume !== undefined) ? snd.volume : 70;
                document.getElementById('snd_volume').value = vol;
                document.getElementById('vol-display').textContent = vol + '%';

                // [v3.3.2] Volume do alarme independente
                let aVol = (snd.alarmVolume !== undefined) ? snd.alarmVolume : 70;
                document.getElementById('snd_alarm_volume').value = aVol;
                document.getElementById('alarm-vol-display').textContent = aVol + '%';

                // [v3.2.52] Popula seletores de melodia
                document.getElementById('mel_touch').value     = snd.melTouch     || 0;
                document.getElementById('mel_confirm').value   = snd.melConfirm   || 0;
                document.getElementById('mel_error').value     = snd.melError     || 0;
                document.getElementById('mel_alarm').value     = snd.melAlarm     || 0;
                document.getElementById('mel_attention').value = snd.melAttention || 0;

                applyLang();

                /* U24 Phase A.2: se há alarms pendentes no sessionStorage,
                 * aplicar overlay em cima do que veio do server. */
                const pAlm = Pending.getSection('alarms');
                if (pAlm) applyPendingToForm(pAlm);

                wireAlarmsPendingListeners();
            } catch(e) {
                showToast('Connection error', 'err');
            }
        }

        /* U24: reconstrói estado atual do form e grava em Pending.alarms. */
        function collectAlarmsState() {
            let sensorData = [];
            document.querySelectorAll('.sensor-card').forEach(card => {
                let idx = parseInt(card.getAttribute('data-idx'));
                let obj = { idx: idx, active: card.querySelector('.alm-active').checked };
                /* One entry per pair of inputs, keyed by channel, holding a
                   two-element min/max array. The inputs
                   are named <key>min / <key>max; the payload groups them, which
                   is the shape the firmware parses and the same shape
                   /api/sensors emits. Previously the four fixed keys went out
                   flat, and a third quantity had nowhere to go. */
                card.querySelectorAll('.alm-input').forEach(inp => {
                    const dk = inp.getAttribute('data-key') || '';
                    const isMax = dk.endsWith('max');
                    const key = dk.slice(0, -3);
                    if (!key) return;
                    if (!obj[key]) obj[key] = [undefined, undefined];
                    obj[key][isMax ? 1 : 0] = parseFloat(inp.value) || 0;
                });
                /* Interlock, per channel. The old pair of hand-written checks
                   also clamped humidity to 100 here; the firmware now holds each
                   channel inside its own plausible range, so the browser only has
                   to keep min below max. */
                Object.keys(obj).forEach(k => {
                    const v = obj[k];
                    if (!Array.isArray(v)) return;
                    if (v[0] !== undefined && v[1] !== undefined && v[0] >= v[1]) {
                        v[1] = Math.round((v[0] + 0.1) * 10) / 10;
                    }
                });
                sensorData.push(obj);
            });
            let soundData = {
                touch:     document.getElementById('snd_touch').checked,
                confirm:   document.getElementById('snd_confirm').checked,
                error:     document.getElementById('snd_error').checked,
                alarm:     document.getElementById('snd_alarm').checked,
                web:       document.getElementById('snd_web').checked,
                attention: document.getElementById('snd_attention').checked,
                mute:      document.getElementById('snd_mute').checked,
                volume:        parseInt(document.getElementById('snd_volume').value),
                alarmVolume:   parseInt(document.getElementById('snd_alarm_volume').value),
                melTouch:      parseInt(document.getElementById('mel_touch').value),
                melConfirm:    parseInt(document.getElementById('mel_confirm').value),
                melError:      parseInt(document.getElementById('mel_error').value),
                melAlarm:      parseInt(document.getElementById('mel_alarm').value),
                melAttention:  parseInt(document.getElementById('mel_attention').value)
            };
            return { sensors: sensorData, sounds: soundData };
        }

        /* U24: aplica valores pendentes do sessionStorage aos campos do form. */
        function applyPendingToForm(pAlm) {
            if (pAlm.sensors && Array.isArray(pAlm.sensors)) {
                pAlm.sensors.forEach(s => {
                    const card = document.querySelector('.sensor-card[data-idx="' + s.idx + '"]');
                    if (!card) return;
                    if (s.active !== undefined) card.querySelector('.alm-active').checked = !!s.active;
                    card.querySelectorAll('.alm-input').forEach(inp => {
                        const k = inp.getAttribute('data-key');
                        if (s[k] !== undefined) inp.value = s[k];
                    });
                });
            }
            if (pAlm.sounds) {
                const snd = pAlm.sounds;
                if (snd.touch !== undefined)     document.getElementById('snd_touch').checked     = !!snd.touch;
                if (snd.confirm !== undefined)   document.getElementById('snd_confirm').checked   = !!snd.confirm;
                if (snd.error !== undefined)     document.getElementById('snd_error').checked     = !!snd.error;
                if (snd.alarm !== undefined)     document.getElementById('snd_alarm').checked     = !!snd.alarm;
                if (snd.web !== undefined)       document.getElementById('snd_web').checked       = !!snd.web;
                if (snd.attention !== undefined) document.getElementById('snd_attention').checked = !!snd.attention;
                if (snd.mute !== undefined)      document.getElementById('snd_mute').checked      = !!snd.mute;
                if (snd.volume !== undefined)      { document.getElementById('snd_volume').value = snd.volume; document.getElementById('vol-display').textContent = snd.volume + '%'; }
                if (snd.alarmVolume !== undefined) { document.getElementById('snd_alarm_volume').value = snd.alarmVolume; document.getElementById('alarm-vol-display').textContent = snd.alarmVolume + '%'; }
                if (snd.melTouch !== undefined)     document.getElementById('mel_touch').value     = snd.melTouch;
                if (snd.melConfirm !== undefined)   document.getElementById('mel_confirm').value   = snd.melConfirm;
                if (snd.melError !== undefined)     document.getElementById('mel_error').value     = snd.melError;
                if (snd.melAlarm !== undefined)     document.getElementById('mel_alarm').value     = snd.melAlarm;
                if (snd.melAttention !== undefined) document.getElementById('mel_attention').value = snd.melAttention;
            }
        }

        /* U24: wires all form inputs to update Pending.alarms on change. */
        function wireAlarmsPendingListeners() {
            if (document.body._almPendingWired) return;
            document.body._almPendingWired = true;
            const handler = () => { Pending.setSection('alarms', collectAlarmsState()); };
            document.body.addEventListener('input', handler);
            document.body.addEventListener('change', handler);
        }

        // ═══════════════════════════════════════════════════════════════
        // UTILITÁRIO: Escapa HTML para evitar XSS
        // ═══════════════════════════════════════════════════════════════
        function escHtml(s) {
            let d = document.createElement('div');
            d.appendChild(document.createTextNode(s));
            return d.innerHTML;
        }

        // ── Volume slider: feedback em tempo real ────────────────────
        document.getElementById('snd_volume').addEventListener('input', function() {
            document.getElementById('vol-display').textContent = this.value + '%';
        });

        // [v3.3.2] Alarm volume slider: feedback em tempo real
        document.getElementById('snd_alarm_volume').addEventListener('input', function() {
            document.getElementById('alarm-vol-display').textContent = this.value + '%';
        });

        // ═══════════════════════════════════════════════════════════════
        // [v3.2.53] WEB AUDIO PREVIEW — Reproduz melodias no navegador
        // Espelha exatamente as frequências e durações do firmware.
        // Usa OscillatorNode com onda quadrada (idêntica ao buzzer PIO).
        // ═══════════════════════════════════════════════════════════════
        var _melodies = {
            touch: [
                [{f:4000,d:15}],
                [{f:2800,d:12},{f:3500,d:12}],
                [{f:5500,d:8}],
                [{f:3200,d:10}],
                [{f:3800,d:10},{f:2800,d:15}],
                [{f:2500,d:8},{f:3200,d:8},{f:4200,d:8}]
            ],
            confirm: [
                [{f:880,d:80},{f:1100,d:80},{f:1320,d:120}],
                [{f:1047,d:90},{f:1319,d:90},{f:1568,d:140}],
                [{f:1200,d:80},{f:0,d:60},{f:1500,d:120}],
                [{f:523,d:70},{f:659,d:70},{f:784,d:70},{f:1047,d:130}],
                [{f:1800,d:50},{f:2400,d:50},{f:1800,d:50},{f:2400,d:100}],
                [{f:784,d:120},{f:1047,d:160}]
            ],
            error: [
                [{f:400,d:150},{f:250,d:220}],
                [{f:300,d:100},{f:0,d:80},{f:300,d:100}],
                [{f:200,d:250},{f:150,d:300}],
                [{f:350,d:120},{f:370,d:120}],
                [{f:500,d:100},{f:420,d:100},{f:340,d:180}],
                [{f:180,d:200}]
            ],
            alarm: [
                [{f:2200,d:180},{f:0,d:100},{f:2200,d:180},{f:0,d:200}],
                [{f:1800,d:200},{f:2400,d:200},{f:1800,d:200}],
                [{f:3000,d:100},{f:0,d:60},{f:3000,d:100}],
                [{f:2600,d:300}],
                [{f:1600,d:150},{f:2000,d:150},{f:2400,d:150}],
                [{f:2800,d:60},{f:0,d:40},{f:2800,d:60},{f:0,d:40},{f:2800,d:60}]
            ],
            attention: [
                [{f:1047,d:90},{f:0,d:50},{f:1319,d:180}],
                [{f:1568,d:70},{f:1319,d:70},{f:1568,d:120}],
                [{f:880,d:60},{f:0,d:30},{f:880,d:60},{f:0,d:30},{f:880,d:100}],
                [{f:1318,d:130},{f:988,d:200}],
                [{f:784,d:60},{f:1047,d:60},{f:1318,d:60},{f:1568,d:100}],
                [{f:698,d:90},{f:0,d:70},{f:1047,d:180}]
            ]
        };
        var _audioCtx = null;
        var _playingNodes = [];

        function testSound(type) {
            var sel = document.getElementById('mel_' + type);
            if (!sel) return;
            var idx = parseInt(sel.value) || 0;
            var notes = (_melodies[type] || [])[idx];
            if (!notes) return;

            // Interrompe som anterior
            _playingNodes.forEach(function(n) { try { n.stop(); } catch(e){} });
            _playingNodes = [];

            // Inicializa AudioContext (requer interação do usuário)
            if (!_audioCtx) _audioCtx = new (window.AudioContext || window.webkitAudioContext)();
            if (_audioCtx.state === 'suspended') _audioCtx.resume();

            // [v3.3.2] Volume proporcional ao slider correto:
            // Alarme usa snd_alarm_volume, demais usam snd_volume (0-100 → 0.0-0.5)
            var volSlider = (type === 'alarm') ? 'snd_alarm_volume' : 'snd_volume';
            var vol = parseInt(document.getElementById(volSlider).value) || 70;
            var gain = _audioCtx.createGain();
            gain.gain.value = vol / 200;
            gain.connect(_audioCtx.destination);

            // Agenda cada nota com offset temporal acumulativo
            var t = _audioCtx.currentTime + 0.01;
            for (var i = 0; i < notes.length; i++) {
                var note = notes[i];
                var dur = note.d / 1000;
                if (note.f > 0) {
                    var osc = _audioCtx.createOscillator();
                    osc.type = 'square';
                    osc.frequency.value = note.f;
                    osc.connect(gain);
                    osc.start(t);
                    osc.stop(t + dur);
                    _playingNodes.push(osc);
                }
                t += dur + 0.002;  // 2ms gap anti-click (espelha firmware)
            }
        }

        // ═══════════════════════════════════════════════════════════════
        // [v3.2.46] INTERTRAVAMENTO MIN/MAX
        // Ao editar MIN, se ficar >= MAX, MAX sobe para MIN + 0.1.
        // Ao editar MAX, se ficar <= MIN, MIN desce para MAX - 0.1.
        // Usa event delegation no container de sensores.
        // ═══════════════════════════════════════════════════════════════
        document.getElementById('sensor-list').addEventListener('input', function(e) {
            if (!e.target.classList.contains('alm-input')) return;
            var card = e.target.closest('.sensor-card');
            if (!card) return;

            var inputs = {};
            card.querySelectorAll('.alm-input').forEach(function(inp) {
                inputs[inp.getAttribute('data-key')] = inp;
            });

            var key = e.target.getAttribute('data-key');
            var val = parseFloat(e.target.value);
            if (isNaN(val)) return;

            /* Interlock against the partner of this field, whatever quantity it
               belongs to. Was a temperature block and a humidity block written
               out separately, which left a third quantity uncoupled — its MIN
               could be dragged above its MAX with nothing objecting. */
            var isMax = key.endsWith('max');
            var partner = inputs[key.slice(0, -3) + (isMax ? 'min' : 'max')];
            if (!partner) return;
            var other = parseFloat(partner.value);
            if (isNaN(other)) return;
            if (!isMax && val >= other) {
                partner.value = (Math.round((val + 0.1) * 10) / 10).toFixed(1);
            } else if (isMax && val <= other) {
                partner.value = (Math.round((val - 0.1) * 10) / 10).toFixed(1);
            }
        });

        // ── Scroll automático para a aba ativa no nav ────────────────
        document.addEventListener('DOMContentLoaded', () => {
            loadAlarms();
            setTimeout(applyLang, 50);
        });
    </script>
</body>
</html>
)raw";


static const char LICENSE_PAGE[] PROGMEM = R"raw(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate">
    <title>SIMUT - License</title>
    <script src="/lang.js"></script>
    <link rel="stylesheet" href="/style.css">
    <style>
        .container { margin: 20px auto; padding: 0 20px 40px; }
        .card { margin-bottom: 20px; }
        h3 { color: var(--acc); border-bottom: 1px solid var(--border); padding-bottom: 10px; margin-top: 0; font-size: 1.05rem; }
        pre { background: var(--bg); border: 1px solid var(--border); border-radius: 8px; padding: 16px; color: var(--sub); font-family: "Cascadia Code", "Fira Code", "JetBrains Mono", monospace; font-size: 0.78rem; line-height: 1.6; overflow-x: auto; white-space: pre-wrap; word-wrap: break-word; }
    </style>
    <script>
        /* window.t/applyLang/setLang/showToast/fetchSafe vem de /lang.js */
    </script>
</head>
<body>
    <script>installTopbar('nav_lic', 'License')</script>

    <div class="container">
        <div class="card">
            <h2 class="page-title" data-i18n="lic_title">Software License</h2>
            <h3 data-i18n="lic_mit">MIT License</h3>
            <pre>MIT License

Copyright (c) 2025 Ângelo Moisés Alves

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the &quot;Software&quot;), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED &quot;AS IS&quot;, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.</pre>
        </div>
        <div class="card">
            <h3 data-i18n="lic_notice">Third-Party Notices</h3>
            <pre>================================================================================
SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
Third-Party Software Notices
================================================================================

This file lists the third-party libraries and components used by SIMUT,
along with their respective licenses and copyright holders. SIMUT itself
is licensed under the MIT License (see LICENSE file).

These libraries are NOT redistributed with SIMUT source code — they are
resolved at build time by the PlatformIO/Arduino build system. This notice
is provided for attribution and license compliance.

================================================================================
1. Arduino-Pico Core
================================================================================
   Description:  Arduino core for Raspberry Pi RP2040/RP2350
   Author:       Earle F. Philhower, III
   License:      LGPL-2.1 (GNU Lesser General Public License v2.1)
   URL:          https://github.com/earlephilhower/arduino-pico
   Components:   Arduino.h, WiFi.h, WebServer.h, DNSServer.h,
                 HTTPClient.h, WiFiClientSecure.h, SerialBT.h,
                 LEAmDNS.h, SPI.h

   Note: The LGPL permits linking with proprietary/MIT-licensed code
   without requiring the SIMUT source to be LGPL. SIMUT does not modify
   the Arduino-Pico core itself.

================================================================================
2. Raspberry Pi Pico SDK
================================================================================
   Description:  Hardware abstraction layer for RP2040
   Author:       Raspberry Pi (Trading) Ltd.
   License:      BSD-3-Clause
   URL:          https://github.com/raspberrypi/pico-sdk
   Components:   hardware/watchdog.h, hardware/pwm.h, hardware/clocks.h,
                 hardware/gpio.h, hardware/sync.h, hardware/structs/timer.h,
                 pico/multicore.h, pico/mutex.h, pico/time.h,
                 pico/unique_id.h, pico/util/queue.h

================================================================================
3. Adafruit GFX Library
================================================================================
   Description:  Core graphics library for Arduino displays
   Author:       Limor Fried / Ladyada (Adafruit Industries)
   Copyright:    Copyright (c) 2012 Adafruit Industries
   License:      BSD (2-Clause)
   URL:          https://github.com/adafruit/Adafruit-GFX-Library
   Components:   Adafruit_GFX.h, GFXcanvas16

================================================================================
4. Adafruit ILI9341
================================================================================
   Description:  Driver library for ILI9341 TFT displays
   Author:       Limor Fried / Ladyada (Adafruit Industries)
   Copyright:    Copyright (c) 2013 Adafruit Industries
   License:      BSD (2-Clause)
   URL:          https://github.com/adafruit/Adafruit_ILI9341
   Components:   Adafruit_ILI9341.h

================================================================================
5. XPT2046_Touchscreen
================================================================================
   Description:  Touchscreen library for XPT2046 controllers
   Author:       Paul Stoffregen
   Copyright:    Copyright (c) 2015 Paul Stoffregen
   License:      MIT
   URL:          https://github.com/PaulStoffregen/XPT2046_Touchscreen
   Components:   XPT2046_Touchscreen.h

================================================================================
6. LittleFS
================================================================================
   Description:  Little fail-safe filesystem for microcontrollers
   Author:       ARM Limited (originally by Christopher Haster)
   License:      BSD-3-Clause
   URL:          https://github.com/littlefs-project/littlefs
   Components:   LittleFS.h (via Arduino-Pico integration)

================================================================================
7. PubSubClient
================================================================================
   Description:  MQTT client library for Arduino
   Author:       Nick O'Leary
   Copyright:    Copyright (c) 2008-2020 Nicholas O'Leary
   License:      MIT
   URL:          https://github.com/knolleary/pubsubclient
   Components:   PubSubClient.h

================================================================================
8. BearSSL
================================================================================
   Description:  SSL/TLS implementation (crypto primitives)
   Author:       Thomas Pornin
   License:      MIT
   URL:          https://bearssl.org/
   Components:   bearssl/bearssl_hash.h, bearssl/bearssl_hmac.h
                 (via Arduino-Pico WiFiClientSecure)

================================================================================
9. GNU FreeFont (FreeSans)
================================================================================
   Description:  Scalable outline fonts (converted to GFX bitmap format)
   Author:       GNU Project / Primoz Peterlin, Steve White
   License:      GPL-3.0 with Font Embedding Exception
   URL:          https://www.gnu.org/software/freefont/
   Components:   Fonts/FreeSans9pt7b.h, Fonts/FreeSansBold9pt7b.h,
                 Fonts/FreeSansBold12pt7b.h, Fonts/FreeSansBold24pt7b.h

   Note: The Font Embedding Exception permits use of these fonts in
   documents and programs without requiring those works to be GPL.
   The fonts are embedded as bitmap data via Adafruit GFX font converter.

================================================================================
10. OneWirePIO_RP2040
================================================================================
   Description:  1-Wire protocol via RP2040 PIO (no bit-banging)
   Author:       Ângelo Moisés Alves
   License:      MIT
   URL:          https://github.com/angeloINTJ/OneWirePIO_RP2040
   Components:   OneWirePIO.h, DS18B20PIO.h

================================================================================
11. DHT22PIO_RP2040
================================================================================
   Description:  DHT22 sensor driver via RP2040 PIO (no bit-banging)
   Author:       Ângelo Moisés Alves
   License:      MIT
   URL:          https://github.com/angeloINTJ/DHT22PIO_RP2040
   Components:   DHT22PIO.h, DHTBus.h

================================================================================
12. BuzzerPIO_RP2040
================================================================================
   Description:  Buzzer/tone generator via RP2040 PIO with hardware volume
                 control (PWM ultrasonic + audio gate, dual SM per PIO block)
   Author:       Ângelo Moisés Alves
   License:      MIT
   URL:          https://github.com/angeloINTJ/BuzzerPIO_RP2040
   Components:   BuzzerPIO_RP2040.h

================================================================================
13. Liberation Sans Bold
================================================================================
   Description:  Typeface whose outlines were traced into the SIMUT wordmark
                 (the "SIMUT" lettering on the login screen). No font file is
                 embedded: only the five glyphs S, I, M, U, T were converted
                 to static SVG paths.
   Author:       Steve Matteson (Ascender Corp.), maintained by Red Hat
   Copyright:    Copyright (c) 2012 Red Hat, Inc.
   License:      SIL Open Font License 1.1
   URL:          https://github.com/liberationfonts/liberation-fonts
   Components:   SVG path data in LOGIN_PAGE

================================================================================

END OF THIRD-PARTY NOTICES</pre>
        </div>
    </div>
    <script>
        document.addEventListener('DOMContentLoaded', () => { setTimeout(applyLang, 50); });
    </script>
</body>
</html>)raw";

/* v3.34.0: F-WEB-DEDUP — CSS comum extraído. Servido em /style.css
 * com Cache-Control max-age=86400 (browser cacheia entre páginas). */
static const char STYLE_CSS[] PROGMEM = R"raw(/* Paleta e as tres caixas que toda pagina autenticada usa. Moravam no
   <style> de cada uma das oito, que gzipam separado — a mesma regra era
   paga oito vezes. Aqui sao pagas uma, e o navegador as guarda por sete
   dias (Cache-Control em WebManager_Util.cpp) em vez de rebaixa-las a
   cada navegacao. Quem diverge (license, alarms, files) mantem so a
   propriedade que muda no proprio <style>, que vem DEPOIS deste arquivo
   no <head> e por isso ainda ganha. */
:root { --bg: #0c0f13; --card: #161b22; --txt: #e9edf2; --sub: #98a6b3; --acc: #06b6d4; --dang: #ef4444; --border: #2a3340; --ok: #22c55e; --warn: #f59e0b; --track: #262e39; color-scheme: dark; }
.container { max-width: 1200px; margin: 30px auto; padding: 0 20px; }
.card { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 24px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.1); }
h2.page-title { margin-top: 0; font-weight: 600; color: var(--txt); font-size: 1.4rem; margin-bottom: 20px; }
:focus-visible { outline: 2px solid var(--acc); outline-offset: 2px; }
/* Botao primario padrao: solido no accent, texto escuro no tema escuro
   (cyan claro pede tinta escura); o claro troca para branco via lang.js. */
.b-pri { background: var(--acc); color: #001318; border: none; padding: 10px 20px; border-radius: 8px; cursor: pointer; font-size: 0.95em; font-weight: 700; transition: 0.2s; }
.b-pri:hover { opacity: 0.9; }
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: var(--bg); color: var(--txt); margin: 0; padding: 0; }
/* ── Hamburger Nav ──────────────────────────────────────── */
.topbar { background: #10151c; border-bottom: 1px solid var(--border); position: sticky; top: 0; z-index: 50; padding: 0 20px; display: flex; justify-content: space-between; align-items: center; min-height: 48px; }
.hamburger { background: none; border: none; color: var(--sub); cursor: pointer; padding: 6px; display: flex; align-items: center; font-size: 1.4rem; }
.brand { font-size: 1.15rem; font-weight: 800; letter-spacing: -0.5px; color: var(--txt); }
.brand span { color: var(--acc); }
.status-pill { display: flex; align-items: center; gap: 6px; font-size: 0.7rem; color: var(--sub); min-width: 0; }
        /* The address is the one thing someone reads off this bar to type on
           another device, so it truncates rather than pushing the bar wide. */
        .status-pill span { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.status-pill .dot { width: 7px; height: 7px; border-radius: 50%; flex-shrink: 0; }
.drawer-bg { position: fixed; inset: 0; background: rgba(0,0,0,0.55); z-index: 80; opacity: 0; pointer-events: none; transition: opacity 0.25s; }
.drawer-bg.open { opacity: 1; pointer-events: auto; }
.drawer { position: fixed; top: 0; left: 0; width: 270px; max-width: 80vw; height: 100%; background: #10151c; border-right: 1px solid var(--border); z-index: 90; transform: translateX(-100%); transition: transform 0.25s ease; display: flex; flex-direction: column; overflow-y: auto; }
.drawer.open { transform: translateX(0); }
.drawer-head { padding: 16px 18px; display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border); }
.drawer-head .brand { font-size: 1.1rem; }
.drawer nav { padding: 10px 10px; flex: 1; }
.drawer nav a { display: flex; align-items: center; gap: 10px; padding: 11px 14px; color: var(--sub); text-decoration: none; font-weight: 600; font-size: 0.88rem; border-radius: 8px; margin-bottom: 2px; transition: background 0.15s, color 0.15s; }
.drawer nav a:hover { color: var(--txt); background: rgba(255,255,255,0.04); }
.drawer nav a.active { color: var(--acc); background: var(--card); }
.drawer nav a .ico { width: 18px; text-align: center; font-size: 1rem; flex-shrink: 0; }
.drawer-bottom { border-top: 1px solid var(--border); padding: 12px 18px; }
.drawer-bottom .lic-link { display: block; padding: 8px 14px; color: var(--sub); text-decoration: none; font-weight: 600; font-size: 0.82rem; border-radius: 8px; margin: -4px -14px 8px; transition: 0.15s; }
.drawer-bottom .lic-link:hover { color: var(--txt); background: rgba(255,255,255,0.04); }
.drawer-bottom .lic-link.active { color: var(--acc); background: var(--card); }
.drawer-footer { display: flex; justify-content: space-between; align-items: center; }
.drawer-footer select { background: transparent; color: var(--sub); border: none; outline: none; font-size: 0.82rem; cursor: pointer; }
.bc { padding: 10px 20px 0; display: flex; align-items: center; gap: 5px; font-size: 0.72rem; }
.bc-root { color: #3f3f46; }
.bc-page { color: var(--sub); font-weight: 600; }
/* top:48px, nao 0: em erro persistente (.warn e .err ficam na tela) o toast
   cobria a topbar inteira — inclusive o hamburguer, unica navegacao no celular.
   A copia inline do force_chpass fica em top:0 de proposito: la nao ha topbar. */
#net-toast { position:fixed;top:48px;left:0;right:0;z-index:9999;text-align:center;padding:10px 20px;font-size:0.85rem;font-weight:600;transform:translateY(-100%);transition:transform .3s,opacity .3s;opacity:0;pointer-events:none; }
#net-toast.show { transform:translateY(0);opacity:1; }
#net-toast.warn { background:linear-gradient(135deg,#92400e,#b45309);color:#fef3c7;border-bottom:2px solid #f59e0b; }
#net-toast.err { background:linear-gradient(135deg,#7f1d1d,#991b1b);color:#fecaca;border-bottom:2px solid #ef4444; }
#net-toast.ok { background:linear-gradient(135deg,#064e3b,#065f46);color:#a7f3d0;border-bottom:2px solid #10b981; }
/* A gaveta media 100% da viewport GRANDE (barra do navegador escondida): com ela
   visivel, o rodape — licenca, idioma, sair — ficava embaixo da barra, e como o
   nav tem flex:1 o overflow-y da gaveta nunca gerava rolagem para alcanca-lo.
   `dvh` e ignorado por navegador antigo, que fica com o height:100% de cima. */
.drawer { height: 100dvh; }
.drawer nav { min-height: 0; overflow-y: auto; }
/* o lang.js posiciona este menu com `left` inline, entao nao da para prende-lo
   por CSS — mas ao menos impede que fique mais largo que a tela. */
.csel-menu { max-width: calc(100vw - 24px); }
/* Item de grid tem min-width:auto por padrao e NAO encolhe abaixo do min-content
   do que carrega — por isso a tabela do painel esticava a coluna inteira (824px
   numa tela de 360) e arrastava a pagina, apesar de o card dela ter overflow-x.
   Com min-width:0 o item cede e o overflow fica contido no card, que rola. */
.layout-grid > * { min-width: 0; }
/* ── Celular ────────────────────────────────────────────── */
@media (max-width: 640px) {
  .topbar { padding: 0 10px; gap: 8px; }
  .topbar .brand { font-size: 1rem; }
  .hamburger { padding: 10px 12px; }
  /* Was `display: none`. That is not fitting the header to the width — it is
     deleting the IP, which is exactly what a phone user opened the page for.
     It stays, and truncates with an ellipsis if the bar runs out of room. */
  .topbar { overflow: hidden; }
  .brand { white-space: nowrap; }
  .bc { padding: 8px 12px 0; }
  /* .container e .card sao redefinidos nas 10 paginas, e o <style> da pagina vem
     DEPOIS deste arquivo no <head> — dai o `body`, para ganhar por especificidade
     em vez de por ordem. padding-left/right em vez do atalho: a /license depende
     do padding-bottom de 40px que o atalho zeraria. */
  body .container { padding-left: 12px; padding-right: 12px; margin: 16px auto; }
  body .card { padding: 16px; }
  /* alvos de toque: a gaveta tinha 39px, o link de licenca 32px, botoes ate 22px */
  button { min-height: 44px; }
  /* excecao: o botao de tema e redondo com height fixo de 30px, e min-height
     vence height — sem isto ele virava uma elipse de 30x44. `body` porque o CSS
     dele e injetado pelo lang.js depois deste arquivo. */
  body #theme-toggle { width: 40px; height: 40px; min-height: 0; }
  .drawer nav a { padding: 14px; }
  .drawer-bottom .lic-link { padding: 12px 14px; }
  /* NAO usar overflow-wrap em celula de tabela: toda tabela do app ja vive num
     contentor com overflow-x, e a tabela tem width:100%. Quebrar em qualquer
     ponto derruba a largura minima da celula para ~1 caractere, entao a tabela
     "cabe" nos 100% e empilha o texto em coluna em vez de deixar o contentor
     rolar. Preferimos o deslize horizontal. So vale onde NAO ha rolagem: */
  .net-stat .val { overflow-wrap: anywhere; }
  /* "Salvar e Reiniciar" e a unica forma de gravar em /config, /network e /users
     (o botao do formulario foi removido em favor dele). No topbar ele e o primeiro
     item a sair da tela — aqui vira barra fixa no rodape. O seletor precisa de
     `body` porque o lang.js injeta #commit-btn depois deste arquivo no <head>. */
  body #commit-btn { position: fixed; left: 10px; right: 10px; bottom: 10px; z-index: 60; min-height: 46px; font-size: 0.95rem; box-shadow: 0 6px 20px rgba(0,0,0,0.55); }
  body.pend { padding-bottom: 68px; }
}
)raw";

static const char LANG_JS[] PROGMEM = R"raw(
    /* F-LANGPACK β: dict.pt vem de GET /api/lang (servido do .lng).
     * EN inline acima cobre overrides; data-en attrs no HTML cobrem o resto. */
    const dict = {
        /* v3.32.5: fallback inline para chaves faltantes no .lng do device
         * (sem uploadfs). Chaves vêm do /api/lang via Object.assign que
         * sobrescreve por cima — se o .lng tiver, prevalece sobre estes. */
        pt: {
            "alm_attention": "Atenção",
            "fil_uploaded": "Upload concluído.",
            "fil_up_err": "Erro no upload.",
            /* Religar histórico (/config → editor de slot). Ficam aqui, e não
               só no .lng, porque o pack vive no LittleFS e não é atualizado
               por um flash de firmware — sem isto o botão sairia em inglês
               num device que já está em português. */
            "sens_hist": "Gravação do histórico",
            "sens_rebind": "Religar histórico agora",
            "sens_rebind_hint": "Um slot criado ou renomeado hoje não tem coluna no arquivo de histórico de hoje, então não é gravado até amanhã. Isto reescreve esse arquivo para todos os slots, preservando os registros já feitos. O dispositivo reinicia ao final.",
            "sens_rebind_confirm": "Reescrever o arquivo de histórico de hoje com os slots atuais?\n\nOs registros já feitos são preservados. O dispositivo reinicia ao final.",
            "sens_rebind_pending": "Salve e Reinicie antes — reescrever agora usaria a configuração anterior.",
            "sens_rebind_busy": "Reescrevendo...",
            "sens_rebind_ok": "Histórico reescrito",
            "sens_rebind_ok_forced": "Histórico recriado",
            "sens_rebind_recs": "registros preservados",
            "sens_rebind_reboot": "Reiniciando...",
            "sens_rebind_force": "Não foi possível ler o arquivo de histórico de hoje, então os registros não podem ser preservados.\n\nRecriar vazio? Os registros de hoje são perdidos. Os dias anteriores não são tocados.",
            "sens_rebind_kept": "Nada foi alterado.",
            "sens_rebind_meas": "medições",
            "sens_rebind_err": "Falha ao reescrever",
            "sens_rebind_unsure": "Sem resposta do dispositivo. Recarregue a página e confira o log antes de tentar de novo.",
            /* v3.34.0: F-CALIB-UI integrado no /dashboard (~10 chaves usadas inline) */
            "usr_pcal": "Calibração",
            "cal_mode": "Modo Calibração",
            "cal_id": "ID",
            "cal_name": "Nome",
            "cal_ref_t": "Ref. (°C)",
            "cal_ref_h": "Ref. (%)",
            "cal_apply": "Atualizar",
            "cal_apply_ok": "Atualizado v",
            "cal_apply_fail": "Falha: ",
            "cal_no_changes": "Nada a alterar.",
            "cal_ntp_no": "NTP não sincronizado",
            /* Curvas de calibração por pontos (/config → editor de slot).
               Inline pelo mesmo motivo do bloco sens_rebind acima: o .lng no
               LittleFS não acompanha o flash do firmware. */
            "cal_pts_hint": "Até 5 pontos por grandeza, cada um ligando a leitura bruta ao valor mostrado por um instrumento confiável. Um ponto aplica um offset constante; mais pontos dobram a correção entre eles, mantida reta além das pontas. Deixe o bruto vazio para captar a leitura ao salvar.",
            "cal_raw": "Bruto",
            "cal_ref": "Referência",
            "cal_corr": "corrigido",
            "cal_cap": "Usar a leitura bruta atual",
            "cal_del": "Remover este ponto",
            "cal_add": "Adicionar ponto",
            "cal_clear": "Remover correção",
            "cal_none": "Sem correção — padrão do sensor.",
            "cal_legacy": "Offset constante",
            "cal_save_hint": "As correções são gravadas no Salvar e Reiniciar. Requer NTP sincronizado.",
            "cal_err_max": "no máximo 5 pontos.",
            "cal_err_num": "todo ponto precisa de bruto e referência numéricos.",
            "cal_err_noread": "sem leitura ao vivo para captar — preencha o valor bruto.",
            "cal_err_rng": "ponto fora da faixa plausível",
            "cal_err_dup": "dois pontos com o mesmo valor bruto.",
            "cal_mode_lbl": "Interpolação",
            "cal_mode_lin": "Reta",
            "cal_mode_cub": "Suave",
            "cal_mode_hint": "Suave é uma cúbica monótona: dobra pelas âncoras sem jamais ultrapassá-las. Precisa de 3+ pontos; com menos, comporta-se como reta."
        },
        en: {
            "hist_load_btn": "Load", "hist_prompt": "Click 'Load' to view system logs.",
            "greet_morning": "Good morning", "greet_afternoon": "Good afternoon", "greet_evening": "Good evening", "greet_hello": "Hello", "greet_logout": "Logout"
        }
    };
    fetch('/api/lang').then(r=>r.json()).then(d=>{Object.assign(dict.pt,d);if(typeof applyLang==='function')applyLang();}).catch(()=>{});

    /* Infra compartilhada (movida das paginas autenticadas individuais p/ ca'). */
    window.t = function(key, def) { let lang = localStorage.getItem('simut_lang') || 'en'; if (lang === 'en' || typeof dict === 'undefined' || !dict[lang] || !dict[lang][key]) return def; return dict[lang][key]; };
    window.applyLang = function() { let lang = localStorage.getItem('simut_lang') || 'en'; document.querySelectorAll('.lang-select').forEach(s => s.value = lang); document.querySelectorAll('[data-i18n]').forEach(el => { let key = el.getAttribute('data-i18n'); if (el.tagName === 'INPUT' && el.hasAttribute('placeholder')) { if (!el.hasAttribute('data-en')) el.setAttribute('data-en', el.getAttribute('placeholder')); } else { if (!el.hasAttribute('data-en')) el.setAttribute('data-en', el.innerHTML); } let text = (lang === 'en' || typeof dict === 'undefined' || !dict[lang] || !dict[lang][key]) ? el.getAttribute('data-en') : dict[lang][key]; if (text !== null && text !== undefined) { if (el.tagName === 'INPUT' && el.hasAttribute('placeholder')) el.setAttribute('placeholder', text); else el.innerHTML = text; } }); };
    window.setLang = function(lang) { localStorage.setItem('simut_lang', lang); applyLang(); if(typeof window.onLangChange === 'function') window.onLangChange(); };
    window.showToast = function(msg, type, ms) { var el = document.getElementById('net-toast'); if (!el) return; el.textContent = msg; el.className = type + ' show'; setTimeout(function() { el.className = ''; }, ms || 3000); };
    window.fetchSafe = function(url, options) { options = options || {}; const timeout = options.timeout || 15000; const retries = (options.retries !== undefined) ? options.retries : 2; function attempt(n) { const ctrl = new AbortController(); const timer = setTimeout(() => ctrl.abort(), timeout); return fetch(url, Object.assign({}, options, { signal: ctrl.signal })).then(function(resp) { clearTimeout(timer); if (!resp.ok && resp.status >= 500 && resp.status !== 503) throw new Error('Server error'); return resp; }).catch(function(err) { clearTimeout(timer); if (n < retries) { var delay = Math.min(1000 * Math.pow(2, n), 8000); return new Promise(resolve => setTimeout(() => resolve(attempt(n + 1)), delay)); } throw err; }); } return attempt(0); };

    /* v3.36.0 (C1+C2): escape HTML para evitar XSS via dados do servidor (sensor name,
     * theme name etc.) injetados em innerHTML. Usar em todo template literal que
     * contenha valor controlável por user privilegiado. */
    window.escHtml = function(s){var d=document.createElement('div');d.textContent=(s===null||s===undefined)?'':String(s);return d.innerHTML;};

    /* escHtml is safe for element TEXT (between tags) but not for attribute
     * values: textContent to innerHTML leaves the quote bytes unescaped, so a
     * name containing one breaks out of a double-quoted attribute. escAttr
     * escapes the five bytes that matter inside one, for the few places a
     * server-controlled string lands in an attribute (the file manager
     * data-nav and checkbox value). Navigation no longer interpolates names
     * into an inline onclick — those became data-nav plus delegation, read
     * back with getAttribute and never parsed as JS (finding M-7).
     *
     * Written as a char loop with hex-escaped quote bytes (\x22 = doublequote,
     * \x27 = quote) and no regex literal on purpose: the build minifier saves
     * strings before stripping comments, so a raw quote here would desync its
     * parser and drop the surrounding code. */
    window.escAttr = function(s){s=String((s===null||s===undefined)?'':s);var o='',i,c;for(i=0;i<s.length;i++){c=s.charAt(i);o+=(c==='&')?'&amp;':(c==='<')?'&lt;':(c==='>')?'&gt;':(c==='\x22')?'&quot;':(c==='\x27')?'&#39;':c;}return o;};

    /* One delegated navigation handler for the whole page: any element carrying
     * data-nav (folder rows, the parent row, breadcrumb links) navigates to its
     * value. The value is a plain DOM attribute string — never executed — which
     * is why it is safe where an inline onclick with an interpolated path was
     * not. Guarded so re-injected page fragments do not stack listeners. */
    if (!window.__fmNavDelegated) {
        window.__fmNavDelegated = true;
        document.addEventListener('click', function(ev){
            var el = ev.target.closest ? ev.target.closest('[data-nav]') : null;
            if (el && typeof fmNavigate === 'function') { ev.preventDefault(); fmNavigate(el.getAttribute('data-nav')); }
        });
    }

    /* One-time temporary passwords returned by commit_all (account add/reset).
     * Shown ONCE: the device reboots seconds later and keeps only the hash, so
     * this modal blocks the auto-reload until the admin dismisses it. The
     * password is rendered as selectable monospace text rather than a
     * copy-to-clipboard button on purpose — navigator.clipboard needs a secure
     * context and the device serves plain HTTP, so a copy button would fail
     * silently and cost the admin the credential. escHtml on both fields: the
     * username is operator-controlled and the value goes into innerHTML. */
    window.showCredsModal = function(creds, onClose) {
        var rows = creds.map(function(c){
            return '<tr><td style="padding:6px 12px 6px 0;color:var(--sub)">' + escHtml(c.u) +
                   '</td><td style="padding:6px 0;font-family:monospace;font-weight:700;font-size:1.05rem;' +
                   'color:var(--acc);user-select:all">' + escHtml(c.p) + '</td></tr>';
        }).join('');
        var ov = document.createElement('div');
        ov.style.cssText = 'position:fixed;inset:0;z-index:9999;background:rgba(0,0,0,.6);' +
            'display:flex;align-items:center;justify-content:center;padding:16px';
        ov.innerHTML =
            '<div style="background:var(--card,#1b2330);border:1px solid var(--border,#334);' +
            'border-radius:10px;max-width:440px;width:100%;padding:20px;box-shadow:0 8px 40px rgba(0,0,0,.5)">' +
            '<h3 style="margin:0 0 4px">' + escHtml(window.t ? window.t('creds_title','Temporary password') : 'Temporary password') + '</h3>' +
            '<p style="margin:0 0 14px;color:var(--warn);font-size:.9rem">' +
            escHtml(window.t ? window.t('creds_warn','Copy it now — it is shown only once. The user must change it on first login.') : 'Copy it now — shown only once. The user must change it on first login.') + '</p>' +
            '<table style="margin:0 0 16px">' + rows + '</table>' +
            '<button id="creds-ok" class="btn" style="width:100%">' +
            escHtml(window.t ? window.t('creds_ok','I saved it — reload') : 'I saved it — reload') + '</button></div>';
        document.body.appendChild(ov);
        ov.querySelector('#creds-ok').addEventListener('click', function(){
            document.body.removeChild(ov);
            if (typeof onClose === 'function') onClose();
        });
    };

    /* v3.34.0: F-WEB-DEDUP — drawer HTML único injetado em runtime.
     * Cada página tem só <div id="drawer-host"></div> em vez do drawer
     * inteiro hardcoded (que ocupava ~2.4KB raw × 8 páginas).
     * Marca o link active baseado em window.location.pathname. */
    /* Trava a rolagem do corpo com a gaveta aberta: sem isto, arrastar sobre o
       fundo escurecido rolava a pagina atras do painel fixo, e fechar deixava o
       usuario num ponto aleatorio do documento.
       NAO redeclarar `function toggleDrawer()` nas paginas: o <script> inline
       roda depois deste arquivo e a declaracao sobrescreve esta — havia 8 copias
       identicas fazendo exatamente isso, e qualquer melhoria aqui virava codigo
       morto. As paginas so chamam, via onclick, resolvido no clique. */
    window.toggleDrawer = function(){var d=document.getElementById('drawer'),b=document.getElementById('drawer-bg');if(d)d.classList.toggle('open');if(b)b.classList.toggle('open');document.body.style.overflow=(d&&d.classList.contains('open'))?'hidden':'';};
    var DRAWER_HTML = '<div class="drawer-bg" id="drawer-bg" onclick="toggleDrawer()"></div>'
        +'<div class="drawer" id="drawer">'
        +'<div class="drawer-head"><div class="brand">SIMUT<span> IoT</span></div><button class="hamburger" onclick="toggleDrawer()" aria-label="Close">✕</button></div>'
        +'<nav>'
        +'<a href="/" ><span class="ico">📊</span><span data-i18n="nav_dash">Dashboard</span></a>'
        +'<a href="/history" ><span class="ico">📈</span><span data-i18n="nav_hist">History &amp; Logs</span></a>'
        +'<a href="/alarms" ><span class="ico">🔔</span><span data-i18n="nav_alm">Alarms &amp; Sounds</span></a>'
        +'<a href="/config" ><span class="ico">⚙️</span><span data-i18n="nav_cfg">System Config</span></a>'
        +'<a href="/network" ><span class="ico">🌐</span><span data-i18n="nav_net">Network</span></a>'
        +'<a href="/users" ><span class="ico">👤</span><span data-i18n="nav_usr">Users</span></a>'
        +'<a href="/files" ><span class="ico">📁</span><span data-i18n="nav_file">Files</span></a>'
        +'</nav>'
        +'<div class="drawer-bottom">'
        +'<a href="/license" class="lic-link" data-i18n="nav_lic">📜 License</a>'
        +'<div class="drawer-footer"><div><span id="greeting" style="color:var(--sub);font-size:0.78rem"></span><div style="margin-top:4px"><select class="lang-select" onchange="setLang(this.value)"><option value="en">🇺🇸 EN</option><option value="pt">🇧🇷 PT</option></select></div></div>'
        +'<a href="/logout" onclick="if(window.Pending)Pending.clear()" style="color:var(--dang);font-size:0.78rem;text-decoration:none;font-weight:600" data-i18n="greet_logout">Logout</a>'
        +'</div></div></div>';
    /* v3.34.1: mapeamento code → emoji bandeira pra seletor dinâmico. */
    var LANG_FLAGS = {pt:'🇧🇷','pt-BR':'🇧🇷','pt-PT':'🇵🇹',es:'🇪🇸','es-ES':'🇪🇸','es-MX':'🇲🇽',en:'🇺🇸','en-US':'🇺🇸','en-GB':'🇬🇧',fr:'🇫🇷',de:'🇩🇪',it:'🇮🇹',ru:'🇷🇺',zh:'🇨🇳',ja:'🇯🇵',ko:'🇰🇷',nl:'🇳🇱',pl:'🇵🇱',sv:'🇸🇪',tr:'🇹🇷',ar:'🇸🇦'};
    function langFlag(code){var c=(code||'').toLowerCase();return LANG_FLAGS[c]||LANG_FLAGS[c.split('-')[0]]||'🌐';}
    function langShort(code){var c=(code||'').split('-')[0].toUpperCase();return c||'??';}
    window.installDrawer = function(){var h=document.getElementById('drawer-host');if(!h)return;h.outerHTML=DRAWER_HTML;var p=window.location.pathname;document.querySelectorAll('.drawer nav a, .drawer .lic-link').forEach(function(a){if(a.getAttribute('href')===p)a.classList.add('active');});
        /* v3.34.1: atualiza seletor de idioma com base no .lng ativo (langCode/langName de /api/perms).
         * Se nenhum .lng está carregado, remove o 2º option (mostra só EN) e força modo EN
         * caso o user tivesse 'pt' no localStorage (evita applyLang falhar em dict.pt vazio). */
        fetch('/api/perms',{credentials:'same-origin'}).then(function(r){return r.json();}).then(function(d){
            var sel=document.querySelector('.drawer .lang-select');if(!sel)return;
            var opts=sel.querySelectorAll('option');
            if(!d||!d.langCode){if(opts.length>=2)opts[1].remove();if(localStorage.getItem('simut_lang')==='pt'){localStorage.setItem('simut_lang','en');if(typeof applyLang==='function')applyLang();}sel.value='en';return;}
            if(opts.length>=2)opts[1].textContent=langFlag(d.langCode)+' '+langShort(d.langCode);
        }).catch(function(){});};
    document.addEventListener('DOMContentLoaded',function(){if(typeof window.installDrawer==='function')window.installDrawer();});

    /* REF-WEB-SKEL: barra de topo e initSession, uma cópia só — terceira rodada
       do movimento que trouxe o drawer (F-WEB-DEDUP) e o Pending (U24 fase D).
       NA HORA DA ANÁLISE, não no DOMContentLoaded como o drawer: esta barra fica
       acima da dobra, e instalada num evento apareceria depois do primeiro
       quadro, empurrando a página para baixo na cara do usuário. A página chama
       installTopbar logo após o <body>; o /lang.js já rodou (a tag não tem
       defer) e o markup entra antes de qualquer pintura. O drawer pode esperar o
       evento porque nasce fechado. insertAdjacentHTML e não document.write:
       escreve no mesmo ponto sem reentrar no analisador. */
    var TOPBAR_HTML = '<div id="net-toast"></div>'
        +'<div class="topbar">'
        +'<div style="display:flex;align-items:center;gap:12px">'
        +'<button class="hamburger" onclick="toggleDrawer()" aria-label="Menu">☰</button>'
        +'<div class="brand">SIMUT<span> IoT</span></div></div>'
        +'<div class="status-pill">'
        +'<div class="dot" id="conn-dot" style="background:var(--track)"></div>'
        +'<span id="status-ip">--</span></div></div>'
        +'<div id="drawer-host"></div>';
    /* key/label são a chave i18n e o texto inglês da trilha — o mesmo par que
       estava no HTML. O applyLang lê o texto inglês do innerHTML na primeira
       passada e guarda em data-en, então o contrato de tradução não muda. */
    window.installTopbar = function(key, label) {
        var html = TOPBAR_HTML
            + '<div class="bc"><span class="bc-root">SIMUT</span>'
            + '<span style="color:#3f3f46">›</span>'
            + '<span class="bc-page" data-i18n="' + key + '">' + label + '</span></div>';
        var s = document.currentScript;
        if (s) s.insertAdjacentHTML('beforebegin', html);
        else if (document.body) document.body.insertAdjacentHTML('afterbegin', html);
    };

    /* Esconde os itens de menu fora da permissão, monta a saudação e acende o
       ponto de conexão. Era idêntica byte a byte nas oito páginas. O ouvinte
       abaixo é registrado DEPOIS do installDrawer de propósito: eles disparam na
       ordem em que entram, e esta função varre `.drawer nav a`, que só existe
       depois que o drawer é instalado. As páginas não chamam mais initSession —
       chamar de novo custa um segundo GET /api/perms, que no RP2040 não é troco. */
    window.initSession = async function() {
        try {
            let r = await fetch('/api/perms', {credentials:'same-origin'});
            let d = await r.json();
            let p = d.perms || 0;
            let user = d.user || '';
            let ntp = d.ntp || 0;
            let time = d.time || 0;
            let navMap = {'/':[1], '/history':[2,4], '/config':[8], '/network':[16], '/users':[256], '/files':[32,64,128], '/alarms':[8], '/license':[1]};
            document.querySelectorAll('.drawer nav a, .drawer .lic-link').forEach(a => {
                let href = a.getAttribute('href');
                let bits = navMap[href];
                if (bits) { let has = bits.some(b => p & b); if (!has) a.style.display = 'none'; }
            });
            let greetEl = document.getElementById('greeting');
            if (greetEl && user) {
                let greet = window.t('greet_hello', 'Hello');
                if (ntp === 1 && time > 1000000000) {
                    let dt = new Date(time * 1000);
                    let h = dt.getHours();
                    if (h >= 5 && h < 12) greet = window.t('greet_morning', 'Good morning');
                    else if (h >= 12 && h < 18) greet = window.t('greet_afternoon', 'Good afternoon');
                    else greet = window.t('greet_evening', 'Good evening');
                }
                greetEl.textContent = greet + ', ' + user;
            }
            let dot = document.getElementById('conn-dot');
            let ipEl = document.getElementById('status-ip');
            if (dot) dot.style.background = 'var(--ok)';
            if (ipEl) { try { let sr = await fetch('/api/status'); let sd = await sr.json(); if(sd.sys) ipEl.textContent = sd.sys.ip || '--'; } catch(e){} }
        } catch(e) { let dot = document.getElementById('conn-dot'); if(dot) dot.style.background = 'var(--dang)'; }
    };
    document.addEventListener('DOMContentLoaded',function(){window.initSession();});

    /* =========================================================================
     * U24 Phase D — Pending Changes Manager + commit-all (shared across pages)
     * =========================================================================
     * Antes: cada página (config/alarms/users/network) tinha uma cópia local
     * deste módulo (~3KB × 4 = ~12KB). Agora centralizado em lang.js (1 cópia
     * carregada por todas as páginas). Páginas read-only (dash/hist/file/lic)
     * herdam o botão automaticamente via installCommitInfra(). */
    window.Pending = {
        data: {},
        init() {
            try { this.data = JSON.parse(sessionStorage.getItem('simut_pending') || '{}'); } catch(e) { this.data = {}; }
            this.refreshUI();
        },
        _persist() { sessionStorage.setItem('simut_pending', JSON.stringify(this.data)); this.refreshUI(); this._maybeNotify(); },
        setField(section, field, value) {
            if (!this.data[section]) this.data[section] = {};
            this.data[section][field] = value;
            this._persist();
        },
        setSection(section, obj) { this.data[section] = obj; this._persist(); },
        getSection(section) { return this.data[section] || {}; },
        pushUserAction(action) {
            if (!this.data.users) this.data.users = { actions: [] };
            this.data.users.actions.push(action);
            this._persist();
        },
        popUserAction() {
            if (!this.data.users || !this.data.users.actions || this.data.users.actions.length === 0) return;
            this.data.users.actions.pop();
            if (this.data.users.actions.length === 0) delete this.data.users;
            sessionStorage.setItem('simut_pending', JSON.stringify(this.data));
            this.refreshUI();
        },
        _maybeNotify() {
            if (!sessionStorage.getItem('simut_pending_notified')) {
                sessionStorage.setItem('simut_pending_notified', '1');
                if (typeof showToast === 'function') {
                    /* Sem "no topo": no celular o botao fica numa barra fixa no
                       rodape, entao a posicao depende da largura da tela. */
                    showToast((window.t ? window.t('pending_notice', 'Click "Save & Restart" to apply your changes.') : 'Click "Save & Restart"'), 'warn', 5000);
                }
            }
        },
        clear() {
            this.data = {};
            sessionStorage.removeItem('simut_pending');
            sessionStorage.removeItem('simut_pending_notified');
            this.refreshUI();
        },
        hasAny() { return Object.keys(this.data).some(k => { const s = this.data[k]; return s && (Array.isArray(s) ? s.length > 0 : Object.keys(s).length > 0); }); },
        refreshUI() {
            const btn = document.getElementById('commit-btn');
            const any = this.hasAny();
            if (btn) btn.style.display = any ? 'inline-block' : 'none';
            /* .pend reserva o espaco da barra fixa de rodape no celular */
            document.body.classList.toggle('pend', any);
        }
    };

    window.commitAll = async function() {
        const msg = (window.t ? window.t('commit_confirm',
            'This will save every change and restart the system.\n\n' +
            '⚠️ The device goes offline for ~10 seconds.\n' +
            'Any history/log write in progress is interrupted.\n\n' +
            'Continue?') : 'Save and restart?');
        if (!confirm(msg)) return;
        const btn = document.getElementById('commit-btn');
        if (btn) { btn.disabled = true; btn.innerText = '...'; }
        /* Apply calibration changes first if pending */
        const pendingCalib = Pending.getSection('calib');
        if (pendingCalib && pendingCalib.sensors && pendingCalib.sensors.length > 0) {
            try {
                const cr = await fetchSafe('/api/calib', { method:'POST', headers:{'Content-Type':'application/json'},
                    body:JSON.stringify({sensors: pendingCalib.sensors || []}), retries:0, timeout:30000 });
                if (cr.ok) {
                    /* Keep calib in Pending.data so commit_all can save it to flash */
                    Pending.data._calibApplied = true;
                    sessionStorage.setItem('simut_pending', JSON.stringify(Pending.data));
                } else {
                    const j = await cr.json().catch(()=>({}));
                    showToast((window.t ? window.t('calib_err','Calibration error: ')+(j.error||cr.status) : 'Calib error: '+(j.error||cr.status)), 'err');
                    if (btn) { btn.disabled = false; btn.innerText = (window.t ? window.t('commit_btn','💾 Save & Restart') : '💾 Save & Restart'); }
                    return;
                }
            } catch(e) {
                showToast((window.t ? window.t('calib_timeout','Calibration timed out. Try again.') : 'Calib timeout. Try again.'), 'err');
                if (btn) { btn.disabled = false; btn.innerText = (window.t ? window.t('commit_btn','💾 Save & Restart') : '💾 Save & Restart'); }
                return;
            }
            /* Wait for rate limit cooldown before commit_all */
            await new Promise(r => setTimeout(r, 1000));
        }
        const currentPort = window.location.port ? parseInt(window.location.port) : (window.location.protocol === 'https:' ? 443 : 80);
        const pendingNet = Pending.getSection('net') || {};
        const localNewPort = pendingNet.web_port ? parseInt(pendingNet.web_port) : 0;
        const redirectPort = (port) => {
            if (!port || port === currentPort) return false;
            showToast((window.t ? window.t('net_saved_port', 'Saved. Redirecting to the new port ' + port + '...') : 'Saved. New port: ' + port), 'ok', 18000);
            setTimeout(() => {
                const proto = window.location.protocol;
                const host = window.location.hostname;
                const portStr = (port === 80 && proto === 'http:') || (port === 443 && proto === 'https:') ? '' : ':' + port;
                window.location.href = proto + '//' + host + portStr + '/';
            }, 15000);
            return true;
        };
        try {
            const fd = new URLSearchParams();
            fd.set('_payload', JSON.stringify(Pending.data));
            const r = await fetchSafe('/api/commit_all', { method: 'POST', body: fd, retries: 0, timeout: 20000 });
            if (r.ok) {
                let j = {}; try { j = await r.json(); } catch(e) {}
                Pending.clear();
                /* One-time passwords for accounts added/reset in this commit.
                 * The device reboots in ~3s and keeps only the hash, so hold
                 * the reload behind the modal — an auto-reload would carry the
                 * credential off-screen before the admin could read it. */
                if (j.creds && j.creds.length) {
                    showCredsModal(j.creds, () => {
                        if (!redirectPort(j.newPort || localNewPort)) window.location.reload();
                    });
                } else if (!redirectPort(j.newPort || localNewPort)) {
                    showToast((window.t ? window.t('commit_saved', 'Saved! Restarting system...') : 'Saved! Restarting...'), 'ok', 20000);
                    setTimeout(() => { window.location.reload(); }, 12000);
                }
            } else {
                /* The server says exactly which slot and which pin it refused
                 * (e.g. "slot 4: GP2 already used by slot 2"). Showing a bare
                 * "Falha ao salvar." threw that away and left the user with a
                 * rejected commit and no idea what to change. */
                /* commit_all authorizes per section, so a 403 names the one it
                 * refused ("Forbidden" + section:"users"). Dropping that field
                 * left an operator staring at a bare "Forbidden" with four
                 * staged sections and no way to tell which one it meant. */
                let detail = ''; try { const j = await r.json(); detail = (j.error || '') + (j.section ? ' (' + j.section + ')' : ''); } catch(e) {}
                showToast((window.t ? window.t('commit_err', 'Save failed.') : 'Save failed.') +
                          (detail ? ' — ' + detail : ''), 'err', 9000);
                if (btn) { btn.disabled = false; btn.innerText = (window.t ? window.t('commit_btn', '💾 Save & Restart') : '💾 Save & Restart'); }
            }
        } catch(e) {
            /* Conexão caiu — assume que reboot começou, limpa e redireciona/reload */
            Pending.clear();
            if (!redirectPort(localNewPort)) {
                showToast((window.t ? window.t('commit_saved', 'Saved! Restarting system...') : 'Saved! Restarting...'), 'ok', 20000);
                setTimeout(() => { window.location.reload(); }, 12000);
            }
        }
    };

    /* Injeta CSS e botão na topbar. Idempotente; chamado por cada página. */
    window.installCommitInfra = function() {
        if (!document.getElementById('commit-btn-css')) {
            const s = document.createElement('style');
            s.id = 'commit-btn-css';
            s.textContent =
                '#commit-btn{background:#16a34a;color:#fff;border:none;padding:7px 14px;border-radius:6px;font-weight:700;font-size:0.82rem;cursor:pointer;display:none}' +
                '#commit-btn:hover{background:#15803d}' +
                '#commit-btn:disabled{opacity:0.6;cursor:wait}' +
                'tr.pending-del{opacity:0.5;text-decoration:line-through}' +
                'tr.pending-add{background:rgba(22,163,74,0.1)}' +
                '.badge.pending{background:#f59e0b;color:#000;font-weight:bold}' +
                '#theme-toggle{background:transparent;border:1px solid var(--border,#27272a);color:var(--sub,#a1a1aa);width:30px;height:30px;border-radius:50%;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;font-size:0.95rem;line-height:1;padding:0;transition:all 0.18s}' +
                '#theme-toggle:hover{color:var(--txt,#f4f4f5);border-color:var(--acc,#06b6d4);transform:scale(1.05)}' +
                /* Version label (span dentro de .brand): fonte menor, peso leve */
                '.brand > span{font-size:0.7rem;font-weight:500;letter-spacing:0.02em;opacity:0.7;margin-left:4px}' +
                /* Tag reference panel em /config — usa var em vez de #18181b */
                '.tag-ref{background:var(--card)}' +
                /* ── Light theme — papel frio de fundo + cartao BRANCO nitido.
                 *    Accent #0072CD (mesmo matiz SIMUT, 4,9:1 = passa AA em texto
                 *    e em botao). --ok/--warn em tons -700 legiveis sobre claro. ── */
                ':root.theme-light{--bg:#f2f5f8;--card:#ffffff;--txt:#1b2733;--sub:#526172;--border:#d9e1e8;--acc:#0072cd;--dang:#c93838;--ok:#15803d;--warn:#b45309;--track:#e3eaf0;color-scheme:light}' +
                'html.theme-light{background:#f2f5f8}' +
                'html.theme-light body{background:var(--bg);color:var(--txt)}' +
                'html.theme-light .topbar{background:#ffffff;border-bottom-color:var(--border)}' +
                'html.theme-light .drawer{background:#ffffff;border-right-color:var(--border)}' +
                'html.theme-light .drawer nav a{color:var(--sub)}' +
                'html.theme-light .drawer nav a:hover{background:rgba(0,150,255,0.08);color:var(--txt)}' +
                'html.theme-light .drawer nav a.active{background:rgba(0,150,255,0.14);color:var(--acc)}' +
                'html.theme-light .drawer-bottom .lic-link{color:var(--sub)}' +
                'html.theme-light .drawer-bottom .lic-link:hover{background:rgba(0,150,255,0.08);color:var(--txt)}' +
                'html.theme-light .drawer-bottom .lic-link.active{background:rgba(0,150,255,0.14);color:var(--acc)}' +
                'html.theme-light .brand{color:var(--txt)}' +
                'html.theme-light .brand span{color:var(--acc)}' +
                'html.theme-light .hamburger{color:var(--sub)}' +
                'html.theme-light .status-pill{color:var(--sub)}' +
                'html.theme-light .card{background:var(--card);color:var(--txt);box-shadow:0 1px 3px rgba(26,37,51,0.06)}' +
                'html.theme-light h2.page-title,html.theme-light h3,html.theme-light label{color:var(--txt)}' +
                'html.theme-light .bc-root{color:#94a3b8}' +
                'html.theme-light .bc-page{color:var(--sub)}' +
                /* Inputs, selects, textareas — mais claros que cards p/ destacar */
                'html.theme-light input[type=text],html.theme-light input[type=password],html.theme-light input[type=number],html.theme-light input[type=search],html.theme-light input[type=date],html.theme-light input[type=time],html.theme-light select,html.theme-light textarea{background:#ffffff;color:var(--txt);border-color:var(--border)}' +
                'html.theme-light #logSearch{background:#ffffff;color:var(--txt)}' +
                'html.theme-light input:focus,html.theme-light select:focus,html.theme-light textarea:focus{border-color:var(--acc);outline:none}' +
                'html.theme-light input::placeholder{color:#94a3b8}' +
                /* Containers de form/grp/cards — tom intermediário p/ separar do card */
                'html.theme-light .frm-box,html.theme-light .grp,html.theme-light .sensor-card,html.theme-light .builder-box,html.theme-light .stats-inline,html.theme-light .sound-item,html.theme-light .vol-row{background:#f4f7fa;border-color:var(--border)}' +
                /* Code / pre / preview — tom mais escuro p/ destacar como bloco */
                'html.theme-light pre,html.theme-light #preview{background:#e8eef4;color:#334155;border-color:var(--border)}' +
                'html.theme-light .highlight{color:var(--acc)}' +
                /* Logs / tabelas */
                'html.theme-light .log-box{background:#f4f7fa;border-color:var(--border)}' +
                'html.theme-light .log-table th{background:#e8eef4;color:var(--sub);border-bottom-color:var(--border)}' +
                'html.theme-light .log-table td{border-bottom-color:var(--border);color:var(--txt)}' +
                'html.theme-light table th,html.theme-light table td{border-bottom-color:var(--border)}' +
                'html.theme-light .log-inf{color:var(--acc)}' +
                /* Chart */
                'html.theme-light .chart-box{background:#ffffff;border-color:var(--border)}' +
                'html.theme-light .chart-overlay{background:rgba(255,255,255,0.92);color:var(--sub)}' +
                /* Buttons / badges */
                'html.theme-light .btn-action{background:#dde4eb;color:var(--txt)}' +
                'html.theme-light .btn-action:hover{background:#c8d2dc}' +
                'html.theme-light .btn-dang{background:transparent;color:var(--dang);border-color:var(--dang)}' +
                'html.theme-light .btn-dang:hover{background:var(--dang);color:#ffffff}' +
                'html.theme-light .bottom-controls button{background:#f4f7fa;color:var(--txt);border-color:var(--border)}' +
                'html.theme-light .bottom-controls button.active{background:var(--acc);color:#ffffff;border-color:var(--acc)}' +
                'html.theme-light .cal-header-row button{background:#f4f7fa;color:var(--txt);border-color:var(--border)}' +
                'html.theme-light .badge{background:#dde4eb;color:var(--txt)}' +
                'html.theme-light .badge.full{background:var(--acc);color:#ffffff}' +
                /* Form submit primário */
                'html.theme-light button[type=submit],html.theme-light .frm-box button[type=submit]{background:var(--acc);color:#ffffff}' +
                'html.theme-light button[type=submit]:disabled{background:#cbd5e1;color:#94a3b8}' +
                /* Calendar */
                'html.theme-light .cal-cell{color:#94a3b8}' +
                'html.theme-light .cal-cell.has-data{background:rgba(0,150,255,0.10);color:var(--acc);border-color:rgba(0,150,255,0.30)}' +
                'html.theme-light .cal-cell.selected{background:var(--acc);color:#ffffff;border-color:var(--acc)}' +
                'html.theme-light .cal-dow{color:var(--sub)}' +
                /* Sound toggle */
                /* O trilho apagado so vale DESLIGADO — a regra generica vencia o
                 * :checked por especificidade e o toggle ligado perdia o accent. */
                'html.theme-light .toggle .slider{background:#c8d2dc}' +
                'html.theme-light .toggle input:checked+.slider{background:var(--acc)}' +
                'html.theme-light .b-pri{color:#fff}' +
                'html.theme-light .btn-test{background:transparent;color:var(--sub);border-color:var(--border)}' +
                'html.theme-light .btn-test:hover{color:var(--acc);border-color:var(--acc)}' +
                /* Progress bars */
                'html.theme-light .progress-track{background:#f4f7fa;border-color:var(--border)}' +
                /* Theme toggle button no light mode */
                'html.theme-light #theme-toggle{color:var(--sub);border-color:var(--border)}' +
                'html.theme-light #theme-toggle:hover{color:var(--acc);border-color:var(--acc)}' +
                /* License page pre com scroll */
                'html.theme-light .lic-link{color:var(--sub)}';
            document.head.appendChild(s);
        }

        const pill = document.querySelector('.topbar .status-pill');
        if (!pill) return;

        /* Ensure flex wrapper around pill so extras (commit btn, theme toggle)
         * align cleanly to the right of status. */
        let wrap = pill.parentNode;
        if (!(wrap.style && wrap.style.display === 'flex')) {
            const w = document.createElement('div');
            w.style.cssText = 'display:flex;align-items:center;gap:12px';
            wrap.replaceChild(w, pill);
            w.appendChild(pill);
            wrap = w;
        }

        /* Commit button (só cria uma vez) */
        if (!document.getElementById('commit-btn')) {
            const btn = document.createElement('button');
            btn.id = 'commit-btn';
            btn.type = 'button';
            btn.onclick = commitAll;
            btn.innerText = (window.t ? window.t('commit_btn', '💾 Save & Restart') : '💾 Save & Restart');
            btn.setAttribute('data-i18n', 'commit_btn');
            wrap.insertBefore(btn, pill);
        }

        /* Theme toggle (só cria uma vez) */
        if (!document.getElementById('theme-toggle')) {
            const tbtn = document.createElement('button');
            tbtn.id = 'theme-toggle';
            tbtn.type = 'button';
            tbtn.onclick = toggleTheme;
            tbtn.setAttribute('aria-label', 'Toggle theme');
            tbtn.setAttribute('title', 'Alternar tema claro/escuro');
            wrap.insertBefore(tbtn, pill);
            /* Atualiza ícone conforme tema corrente */
            _refreshThemeIcon();
        }

        if (window.Pending) Pending.refreshUI();
    };

    /* Tema claro/escuro — preferência do user (localStorage, por browser).
     * applyTheme é chamado cedo (antes de DOMContentLoaded) pra evitar
     * flash de tema errado. Fallback: dark. */
    function _refreshThemeIcon() {
        const btn = document.getElementById('theme-toggle');
        if (!btn) return;
        const isLight = document.documentElement.classList.contains('theme-light');
        /* Mostra o ícone do DESTINO (clique alterna pra este). */
        btn.innerText = isLight ? '🌙' : '☀';
    }
    window.applyTheme = function(t) {
        const root = document.documentElement;
        if (t === 'light') root.classList.add('theme-light');
        else root.classList.remove('theme-light');
        _refreshThemeIcon();
    };
    window.toggleTheme = function() {
        const cur = localStorage.getItem('simut_ui_theme') || 'dark';
        const next = (cur === 'dark') ? 'light' : 'dark';
        localStorage.setItem('simut_ui_theme', next);
        applyTheme(next);
    };
    /* Apply saved theme ASAP (antes de DOMContentLoaded) */
    applyTheme(localStorage.getItem('simut_ui_theme') || 'dark');

    /* Versão: lê do /api/perms e coloca ao lado de "SIMUT" na topbar.
     * Rola sempre que a página carrega; se endpoint falhar, mantém " IoT". */
    window.applyVersion = function(v) {
        if (!v) return;
        document.querySelectorAll('.brand > span').forEach(s => {
            s.textContent = ' ' + v;
        });
    };

    /* CSS global: dropdown custom + toggle switch + sem spinners em number */
    (function(){const c='.csel{position:relative;display:inline-block;vertical-align:middle}.csel-btn{background:var(--bg);color:var(--txt);border:1px solid var(--border);padding:8px 12px;border-radius:6px;cursor:pointer;font-size:0.9rem;outline:none;width:100%;text-align:left;box-sizing:border-box;font-weight:500}.csel-btn:hover{border-color:var(--acc)}.csel-btn .csel-arr{float:right;opacity:0.7}.csel-menu{position:fixed;background:var(--card);border:1px solid var(--border);border-radius:6px;max-height:260px;overflow-y:auto;z-index:9999;padding:4px;box-shadow:0 6px 16px rgba(0,0,0,0.5)}.csel-item{padding:8px 12px;color:var(--txt);cursor:pointer;font-size:0.85rem;border-radius:4px}.csel-item:hover{background:var(--bg)}.csel-item.active{background:var(--acc);color:#000;font-weight:700}'
      + '.toggle{position:relative;display:inline-block;width:44px;height:24px;flex-shrink:0}.toggle input{opacity:0;width:0;height:0}.toggle .slider{position:absolute;cursor:pointer;inset:0;background:#3f3f46;border-radius:24px;transition:.3s}.toggle .slider:before{content:"";position:absolute;height:18px;width:18px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.3s}.toggle input:checked+.slider{background:var(--acc)}.toggle input:checked+.slider:before{transform:translateX(20px)}'
      + 'input[type=number]::-webkit-inner-spin-button,input[type=number]::-webkit-outer-spin-button{-webkit-appearance:none;margin:0}input[type=number]{-moz-appearance:textfield;appearance:textfield}';const s=document.createElement('style');s.textContent=c;document.head.appendChild(s);})();
    document.addEventListener('click',e=>{if(!e.target.closest('.csel'))document.querySelectorAll('.csel-menu').forEach(m=>m.style.display='none');});
    /* O menu e position:fixed com coordenadas congeladas no momento da abertura,
       e so `click` o fechava. No toque, rolar nao e clique: o menu ficava boiando
       sobre conteudo alheio enquanto o botao dele ia embora. Rolou, fecha.
       Sem captura de proposito — rolar DENTRO do menu nao dispara no window. */
    window.addEventListener('scroll',function(){document.querySelectorAll('.csel-menu').forEach(m=>m.style.display='none');},{passive:true});
    /* Setter prototypal capturado UMA vez — bypass do override para uso interno */
    const _selProtoValSet=Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype,'value').set;
    window._cselSync=function(sel){const w=sel._cw;if(!w)return;const m=w.querySelector('.csel-menu'),b=w.querySelector('.csel-btn');m.innerHTML='';let lbl='';Array.from(sel.options).forEach(o=>{const i=document.createElement('div');i.className='csel-item'+(o.selected?' active':'');i.textContent=o.text;i.onclick=()=>{if(sel.disabled)return;/* bypass override: nao re-renderiza menu durante o handler (i nao vira orfao) */ _selProtoValSet.call(sel,o.value);m.querySelectorAll('.csel-item').forEach(x=>x.classList.remove('active'));i.classList.add('active');b.firstChild.textContent=o.text;m.style.display='none';sel.dispatchEvent(new Event('change',{bubbles:true}));};m.appendChild(i);if(o.selected)lbl=o.text;});if(!lbl&&sel.options.length)lbl=sel.options[0].text;b.firstChild.textContent=lbl;};
    window._makeCustomSelect=function(sel){if(!sel||sel.dataset.cd==='1'||sel.multiple)return;sel.dataset.cd='1';const w=document.createElement('div');w.className='csel'+(sel.classList.contains('mel-sel')?' csel-mel':'');const b=document.createElement('button');b.type='button';b.className='csel-btn';const ln=document.createTextNode(''),an=document.createElement('span');an.className='csel-arr';an.textContent='▾';b.appendChild(ln);b.appendChild(an);const m=document.createElement('div');m.className='csel-menu';m.style.display='none';b.onclick=ev=>{ev.stopPropagation();if(sel.disabled||b.disabled)return;const op=m.style.display==='block';document.querySelectorAll('.csel-menu').forEach(x=>{if(x!==m)x.style.display='none';});if(op){m.style.display='none';return;}/* position:fixed: posiciona no viewport para escapar de overflow:hidden de cards parent */ const r=b.getBoundingClientRect();m.style.left=r.left+'px';m.style.minWidth=r.width+'px';const sb=window.innerHeight-r.bottom;if(sb>=200||sb>=r.top){m.style.top=(r.bottom+4)+'px';m.style.bottom='auto';}else{m.style.top='auto';m.style.bottom=(window.innerHeight-r.top+4)+'px';}m.style.display='block';};sel._cw=w;sel.parentNode.insertBefore(w,sel);w.appendChild(b);w.appendChild(m);w.appendChild(sel);sel.style.display='none';_cselSync(sel);const d=Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype,'value');if(d&&d.set)Object.defineProperty(sel,'value',{get(){return d.get.call(this);},set(v){d.set.call(this,v);_cselSync(this);}});};

    /* Ordem importa: install PRIMEIRO (cria botões), depois init
     * (Pending.refreshUI encontra o botão e mostra/esconde). */
    document.addEventListener('DOMContentLoaded', () => {
        if (window.installCommitInfra) installCommitInfra();
        if (window.Pending) Pending.init();
        /* Converte todos os <select> em dropdown custom (skipa multi-select). */
        document.querySelectorAll('select').forEach(_makeCustomSelect);
        /* Busca versão e aplica no brand. Usa perms que todas as páginas
         * já chamam; essa chamada é barata e cacheável se quisermos depois. */
        fetch('/api/perms', { credentials: 'same-origin' })
            .then(r => r.ok ? r.json() : null)
            .then(d => { if (d && d.version) applyVersion(d.version); })
            .catch(() => {});
    });
)raw";


}
