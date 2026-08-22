#!/usr/bin/env node
/* https_tests.js — §5.5/§5.7 sobre HTTPS (imagem release, 1 cliente TLS por vez).
 *
 * O servidor serve UM cliente TLS por vez (MANUAL §6): conexões paralelas são
 * derrubadas. Este teste imita um usuário real: uma página, navegação serial,
 * settles e retries. Não faz churn de contextos.
 *
 *  1. Login real (errada -> erro; certa -> dashboard).
 *  2. Painel HTTPS: escuro + dados vivos + sem erro de console + screenshot.
 *  3. 12 rotas autenticadas sobre HTTPS, timings registrados.
 *  4. Chave keep-alive visível e ON (par TLS presente).
 */
'use strict';
const fs = require('fs');
const path = require('path');
const pup = require('/home/angelo/Documentos/simut/scratchpad/h5g_20260818/node_modules/puppeteer-core');

const BASE = 'https://192.168.3.24';
const OUT = path.join(__dirname, 'shots');
fs.mkdirSync(OUT, { recursive: true });
const settle = ms => new Promise(r => setTimeout(r, ms));

const txt = fs.readFileSync('/home/angelo/Documentos/simut/scratchpad/rig_secrets.py', 'utf8');
const g = k => (txt.match(new RegExp(`${k}\\s*=\\s*'([^']*)'`)) || [])[1];
const USER = g('USER'), PASS = g('PASS');

let passCount = 0, failCount = 0;
function rep(ok, name, detail) {
    console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${name}${detail ? ' — ' + detail : ''}`);
    if (ok) passCount++; else failCount++;
}

async function gotoR(page, url) {
    for (let a = 0; a < 5; a++) {
        try {
            return await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 });
        } catch (e) {
            await settle(2000);
        }
    }
    throw new Error('goto falhou 5x: ' + url);
}

async function login(page, user, pass) {
    await gotoR(page, BASE + '/login');
    await page.waitForSelector('input[name=user]', { timeout: 15000 });
    await settle(800);
    await page.type('input[name=user]', user);
    await page.type('#passInput', pass);
    await page.click('#btnLogin');
    const t0 = Date.now();
    while (Date.now() - t0 < 20000) {
        try {
            const st = await page.evaluate(() => ({
                url: location.href,
                err: (document.getElementById('errMsg') || { textContent: '' }).textContent,
            }));
            if (st.url !== BASE + '/login' && !st.url.includes('/login?')) {
                return { ok: true, err: '' };
            }
            if (st.err.length > 0) return { ok: false, err: st.err };
        } catch (e) { /* navegando */ }
        await settle(300);
    }
    return { ok: false, err: 'timeout' };
}

(async () => {
    const browser = await pup.launch({
        executablePath: '/usr/bin/google-chrome',
        headless: 'new',
        args: ['--no-sandbox', '--disable-gpu', '--ignore-certificate-errors'],
    });
    const page = await browser.newPage();
    const errs = [];
    page.on('pageerror', e => errs.push('pageerror: ' + e.message));
    page.on('console', m => { if (m.type() === 'error') errs.push('console: ' + m.text()); });
    page.on('requestfailed', r => errs.push('requestfailed: ' + r.url()));
    page.on('response', r => { if (r.status() >= 400) errs.push(`HTTP ${r.status()}: ${r.url()}`); });

    // 1. login real
    console.log('[1] Login navegador real (HTTPS)');
    const bad = await login(page, USER, 'senha-errada');
    rep(bad.ok === false && bad.err.length > 0, 'senha errada recusada com erro visível',
        `"${bad.err.slice(0, 50)}"`);
    await settle(4000); // lockout 2 s + folga
    errs.length = 0;
    const good = await login(page, USER, PASS);
    rep(good.ok && (page.url() === BASE + '/' || page.url() === BASE),
        'senha correta entra no dashboard', page.url());
    await settle(2500);
    if (errs.length) rep(false, 'login sem erro de console', errs.slice(0, 3).join(' | '));
    else rep(true, 'login sem erro de console');

    // 2. painel sob HTTPS
    console.log('[2] Painel sob HTTPS');
    errs.length = 0;
    await gotoR(page, BASE + '/');
    await settle(3000);
    const dash = await page.evaluate(() => {
        const pill = document.querySelector('.status-pill, [class*=pill]');
        const cards = document.querySelectorAll('.card, .sensor-card');
        let dataLive = false;
        document.querySelectorAll('*').forEach(el => {
            if (el.children.length === 0 && /\d/.test(el.textContent || '')) dataLive = true;
        });
        return {
            bg: getComputedStyle(document.documentElement).backgroundColor,
            pill: pill ? pill.textContent : null,
            cards: cards.length,
            dataLive,
        };
    });
    await page.screenshot({ path: path.join(OUT, 'https_dashboard.png') });
    rep(dash.bg === 'rgb(12, 15, 19)', 'painel estilizado (tema escuro)', dash.bg);
    rep(dash.dataLive, 'dados vivos no painel', `cards=${dash.cards}`);
    rep(errs.length === 0, 'sem erro de console', errs.slice(0, 3).join(' | ') || 'nenhum');
    rep(fs.existsSync(path.join(OUT, 'https_dashboard.png')), 'screenshot capturado');

    // 3. 12 rotas autenticadas
    console.log('[3] 12 rotas autenticadas (HTTPS)');
    const routes = ['/', '/history', '/alarms', '/config', '/network', '/users',
                    '/files', '/license', '/login', '/lang.js', '/style.css', '/favicon.ico'];
    const timings = [];
    for (const route of routes) {
        errs.length = 0;
        const t0 = Date.now();
        if (route.startsWith('/lang') || route.startsWith('/style') || route.startsWith('/favicon')) {
            const r = await page.evaluate(async u => {
                const resp = await fetch(u);
                return { status: resp.status, size: (await resp.arrayBuffer()).byteLength };
            }, route);
            rep(r.status === 200, `${route} (estático)`, `HTTP ${r.status}, ${r.size} B`);
        } else {
            await gotoR(page, BASE + route);
            await settle(2000);
            const nav = await page.evaluate(() => {
                const n = performance.getEntriesByType('navigation')[0];
                return n ? Math.round(n.duration) : -1;
            });
            timings.push({ route, nav, wall: Date.now() - t0 });
            rep(true, `${route} carrega`, `${Date.now() - t0} ms (nav ${nav} ms)`);
        }
        if (errs.length) rep(false, `${route} sem erro/404`, errs.slice(0, 3).join(' | '));
    }
    console.log(`  timing (navigation.duration ms): ` +
        timings.map(x => `${x.route}:${x.nav}`).join(' '));

    // 4. chave keep-alive
    console.log('[4] Chave keep-alive');
    await gotoR(page, BASE + '/network');
    await settle(2000);
    const ka = await page.evaluate(() => {
        const row = document.getElementById('web_ka_row');
        const chk = document.getElementById('web_ka');
        return {
            visible: row && getComputedStyle(row).display !== 'none',
            checked: chk ? chk.checked : null,
        };
    });
    rep(ka.visible === true, 'chave visível com par TLS', JSON.stringify(ka));
    rep(ka.checked === true, 'default ON', 'checked=' + ka.checked);

    await browser.close();
    console.log(`\n== RESULTADO: ${passCount} passaram, ${failCount} falharam ==`);
    process.exit(failCount ? 1 : 0);
})().catch(e => { console.error('FATAL:', e.message); process.exit(2); });
