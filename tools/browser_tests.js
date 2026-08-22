#!/usr/bin/env node
/* browser_tests.js — §5.5/§5.7 navegador real (PLANO-VALIDACAO v2.3.2).
 *
 * Uso:  node browser_tests.js [http|https]
 *
 * T1  Login real 5/5: senha errada recusada com erro visível, senha certa
 *     entra no dashboard (JS da página faz nonce+sha256).
 * T2  12 rotas autenticadas: 9 páginas + 3 estáticos, sem erro de console,
 *     sem requestfailed, sem resposta >=400; timing de navegação registrado
 *     (1ª vs reuso de conexão).
 * T3  Piscada branca (v2.3.2): /style.css e /lang.js atrasados 2,5 s — a
 *     1ª pintura tem de nascer escura pelos tokens inline (html/body =
 *     rgb(12, 15, 19)) nas 8 páginas.
 * T4  Tema claro: localStorage simut_ui_theme=light -> html rgb(242,245,248),
 *     sem erro; volta ao escuro sem regressão.
 * T5  Chave keep-alive: /network #web_ka_row visível SÓ com par TLS; default
 *     ON quando visível.
 *
 * Credenciais: SIMUT_WEB_USER/SIMUT_WEB_PASS ou scratchpad/rig_secrets.py.
 */
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const pup = require('/home/angelo/Documentos/simut/scratchpad/h5g_20260818/node_modules/puppeteer-core');

const HOST = '192.168.3.24';
const SCHEME = (process.argv[2] || 'http').toLowerCase();
const BASE = `${SCHEME}://${HOST}`;
const OUT = path.join(__dirname, 'shots');
fs.mkdirSync(OUT, { recursive: true });

const PAGES = ['/', '/history', '/alarms', '/config', '/network', '/users',
               '/files', '/license', '/login'];
const STATIC = ['/lang.js', '/style.css', '/favicon.ico'];

function creds() {
    if (process.env.SIMUT_WEB_USER && process.env.SIMUT_WEB_PASS) {
        return [process.env.SIMUT_WEB_USER, process.env.SIMUT_WEB_PASS];
    }
    const txt = fs.readFileSync(
        '/home/angelo/Documentos/simut/scratchpad/rig_secrets.py', 'utf8');
    const g = k => (txt.match(new RegExp(`${k}\\s*=\\s*'([^']*)'`)) || [])[1];
    return [g('USER'), g('PASS')];
}
const [USER, PASS] = creds();

let passCount = 0, failCount = 0;
function rep(ok, name, detail) {
    console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${name}${detail ? ' — ' + detail : ''}`);
    if (ok) passCount++; else failCount++;
}

function attach(page) {
    const errs = [];
    page.on('pageerror', e => errs.push('pageerror: ' + (e.stack ? e.stack.split('\n').slice(0, 3).join(' <- ') : e.message)));
    page.on('console', m => { if (m.type() === 'error') errs.push('console: ' + m.text()); });
    page.on('requestfailed', r => errs.push('requestfailed: ' + r.url()));
    page.on('response', r => {
        if (r.status() >= 400) errs.push(`HTTP ${r.status()}: ${r.url()}`);
    });
    return errs;
}

async function loginViaForm(page, user, pass) {
    await page.goto(BASE + '/login', { waitUntil: 'domcontentloaded', timeout: 30000 });
    await page.waitForSelector('input[name=user]', { timeout: 10000 });
    await new Promise(r => setTimeout(r, 600));
    await page.type('input[name=user]', user);
    await page.type('#passInput', pass);
    await page.click('#btnLogin');
    // poll guardado: espera navegação OU mensagem de erro; nunca deixa task
    // pendurada na página (um waitForFunction poluía o documento novo).
    const t0 = Date.now();
    while (Date.now() - t0 < 15000) {
        try {
            const st = await page.evaluate(() => ({
                url: location.href,
                err: (document.getElementById('errMsg') || { textContent: '' }).textContent,
            }));
            if (st.url !== BASE + '/login' && !st.url.includes('/login?')) {
                return { url: st.url, err: '' };
            }
            if (st.err.length > 0) return { url: st.url, err: st.err };
        } catch (e) { /* contexto trocado durante a navegação */ }
        await new Promise(r => setTimeout(r, 200));
    }
    return { url: page.url(), err: '' };
}

async function gotoR(page, url, opts) {
    for (let attempt = 0; attempt < 3; attempt++) {
        try {
            return await page.goto(url, opts);
        } catch (e) {
            if (attempt === 2) throw e;
            await new Promise(r => setTimeout(r, 1500));
        }
    }
}

async function waitUnlocked(page) {
    await page.goto(BASE + '/login', { waitUntil: 'domcontentloaded', timeout: 20000 })
        .catch(() => null);
    for (let i = 0; i < 40; i++) {
        try {
            const j = await page.evaluate(async () =>
                (await (await fetch('/api/login_init')).json()));
            if (!j.locked && (j.lockSec || 0) <= 0) return;
        } catch (e) { /* página não responde — espera */ }
        await new Promise(r => setTimeout(r, 1000));
    }
}

(async () => {
    const browser = await pup.launch({
        executablePath: '/usr/bin/google-chrome',
        headless: 'new',
        args: ['--no-sandbox', '--disable-gpu', '--ignore-certificate-errors'],
    });

    // ── T1: login real 5/5 ────────────────────────────────────────────────
    console.log('[T1] Login navegador real');
    for (let i = 1; i <= 5; i++) {
        const ctx = await browser.createBrowserContext();
        const page = await ctx.newPage();
        const errs = attach(page);
        await waitUnlocked(page);
        const bad = await loginViaForm(page, USER, 'senha-errada-' + i);
        const errVisible = bad.err.length > 0;
        errs.length = 0; // o 401 do login errado é deliberado — conta só o sucesso
        await waitUnlocked(page); // backoff exponencial: (1<<fail)×1 s
        const good = await loginViaForm(page, USER, PASS);
        const onDash = good.url === BASE + '/' || good.url === BASE;
        rep(errVisible, `rodada ${i}: senha errada recusada com erro visível`,
            errVisible ? `"${bad.err.slice(0, 60)}"` : 'sem mensagem de erro');
        rep(onDash, `rodada ${i}: senha correta entra no dashboard`, good.url);
        await page.goto(BASE + '/logout', { waitUntil: 'domcontentloaded' }).catch(() => null);
        if (errs.length) rep(false, `rodada ${i}: sem erro de console`, errs.join(' | '));
        await ctx.close();
    }

    // ── T2: 12 rotas autenticadas ─────────────────────────────────────────
    console.log('[T2] Páginas autenticadas (12 rotas)');
    const ctx2 = await browser.createBrowserContext();
    const page2 = await ctx2.newPage();
    const errs2 = attach(page2);
    await waitUnlocked(page2);
    await loginViaForm(page2, USER, PASS);
    const timings = [];
    for (const route of [...PAGES, ...STATIC]) {
        errs2.length = 0;
        const t0 = Date.now();
        if (route.startsWith('/api') || STATIC.includes(route)) {
            const r = await page2.evaluate(async u => {
                const resp = await fetch(u);
                return { status: resp.status, size: (await resp.arrayBuffer()).byteLength };
            }, route);
            rep(r.status === 200, `${route} (estático)`, `HTTP ${r.status}, ${r.size} B`);
        } else {
            await gotoR(page2, BASE + route, { waitUntil: 'domcontentloaded', timeout: 30000 });
            await new Promise(r => setTimeout(r, 1200));
            const t = Date.now() - t0;
            const nav = await page2.evaluate(() => {
                const n = performance.getEntriesByType('navigation')[0];
                return n ? Math.round(n.duration) : -1;
            });
            timings.push({ route, wall: t, nav });
            rep(true, `${route} carrega`, `${t} ms (nav ${nav} ms)`);
        }
        if (errs2.length) rep(false, `${route} sem erro/404`, errs2.slice(0, 3).join(' | '));
    }
    console.log(`  timing de navegação (ms, performance.navigation.duration): ` +
        timings.map(x => `${x.route}:${x.nav}`).join(' '));

    // ── T3: piscada branca (1ª pintura escura, assets atrasados) ──────────
    console.log('[T3] Primeira pintura escura (style.css/lang.js atrasados 2,5 s)');
    for (const route of PAGES.filter(p => p !== '/login')) {
        const page3 = await browser.newPage();
        const errs3 = attach(page3);
        await page3.setRequestInterception(true);
        page3.on('request', req => {
            const u = req.url();
            if (u.includes('/style.css') || u.includes('/lang.js')) {
                setTimeout(() => req.continue().catch(() => null), 2500);
            } else req.continue().catch(() => null);
        });
        await waitUnlocked(page3);
        await loginViaForm(page3, USER, PASS);
        const shot = path.join(OUT, `dark_first_${route.replace(/\//g, '_')}.png`);
        await gotoR(page3, BASE + route, { waitUntil: 'domcontentloaded', timeout: 30000 });
        await new Promise(r => setTimeout(r, 400)); // assets ainda presos
        const bg = await page3.evaluate(() => ({
            html: getComputedStyle(document.documentElement).backgroundColor,
            body: getComputedStyle(document.body).backgroundColor,
        }));
        await page3.screenshot({ path: shot });
        rep(bg.html === 'rgb(12, 15, 19)', `${route} html nasce escuro`, bg.html);
        rep(bg.body === 'rgb(12, 15, 19)', `${route} body nasce escuro`, bg.body);
        if (errs3.length) rep(false, `${route} sem erro`, errs3.slice(0, 2).join(' | '));
        await page3.close();
    }

    // ── T4: tema claro sem regressão ──────────────────────────────────────
    console.log('[T4] Tema claro');
    const ctx4 = await browser.createBrowserContext();
    const page4 = await ctx4.newPage();
    const errs4 = attach(page4);
    await waitUnlocked(page4);
    await page4.evaluate(() => { localStorage.setItem('simut_ui_theme', 'light'); });
    await loginViaForm(page4, USER, PASS);
    await gotoR(page4, BASE + '/', { waitUntil: 'domcontentloaded', timeout: 30000 });
    await new Promise(r => setTimeout(r, 1500));
    const light = await page4.evaluate(() => ({
        html: getComputedStyle(document.documentElement).backgroundColor,
        cls: document.documentElement.classList.contains('theme-light'),
        card: getComputedStyle(document.querySelector('.card') || document.body).backgroundColor,
    }));
    rep(light.html === 'rgb(242, 245, 248)' && light.cls,
        'claro aplicado (html #f2f5f8 + .theme-light)', JSON.stringify(light));
    await page4.evaluate(() => { localStorage.removeItem('simut_ui_theme'); });
    await page4.reload({ waitUntil: 'domcontentloaded' });
    await new Promise(r => setTimeout(r, 1500));
    const back = await page4.evaluate(() => ({
        bg: getComputedStyle(document.documentElement).backgroundColor,
        cls: document.documentElement.classList.contains('theme-light'),
    }));
    rep(back.bg === 'rgb(12, 15, 19)' && !back.cls, 'volta ao escuro sem regressão', JSON.stringify(back));
    if (errs4.length) rep(false, 'claro sem erro de console', errs4.slice(0, 2).join(' | '));
    await ctx4.close();

    // ── T5: chave keep-alive (só com par TLS) ─────────────────────────────
    console.log('[T5] Chave keep-alive na UI');
    const ctx5 = await browser.createBrowserContext();
    const page5 = await ctx5.newPage();
    await waitUnlocked(page5);
    await loginViaForm(page5, USER, PASS);
    await gotoR(page5, BASE + '/network', { waitUntil: 'domcontentloaded', timeout: 30000 });
    await new Promise(r => setTimeout(r, 1500));
    const ka = await page5.evaluate(() => {
        const row = document.getElementById('web_ka_row');
        const chk = document.getElementById('web_ka');
        return {
            visible: row && getComputedStyle(row).display !== 'none',
            checked: chk ? chk.checked : null,
        };
    });
    const tls = await page5.evaluate(async () => (await (await fetch('/api/network')).json()).web_tls);
    if (SCHEME === 'https' || tls) {
        rep(ka.visible === true, 'chave visível com par TLS', JSON.stringify(ka));
        rep(ka.checked === true, 'default ON', 'checked=' + ka.checked);
    } else {
        rep(ka.visible === false, 'chave oculta sem par TLS', JSON.stringify(ka));
    }
    await ctx5.close();

    await browser.close();
    console.log(`\n== RESULTADO: ${passCount} passaram, ${failCount} falharam ==`);
    process.exit(failCount ? 1 : 0);
})().catch(e => { console.error('FATAL:', e); process.exit(2); });
