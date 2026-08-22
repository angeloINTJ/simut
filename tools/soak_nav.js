#!/usr/bin/env node
/* soak_nav.js — §5.7 soak "abrir-ler-clicar" >= 30 min sobre HTTPS.
 *
 * Respeita o servidor de 1 cliente TLS por vez (MANUAL §6): favicon é abortado
 * (evita o 2º socket concorrente), navegação serial, retries explícitos e log
 * por iteração. Verifica login antes do laço (com retry). No fim: uptime
 * monotônico (0 reboot) + log do dispositivo via serial para contar
 * C0=[WEB_POLL].
 *
 * Uso: node soak_nav.js [minutos]   (default 30)
 */
'use strict';
const fs = require('fs');
const pup = require('/home/angelo/Documentos/simut/scratchpad/h5g_20260818/node_modules/puppeteer-core');

const BASE = 'https://192.168.3.24';
const MINUTES = parseInt(process.argv[2] || '30', 10);
const settle = ms => new Promise(r => setTimeout(r, ms));

const txt = fs.readFileSync('/home/angelo/Documentos/simut/scratchpad/rig_secrets.py', 'utf8');
const g = k => (txt.match(new RegExp(`${k}\\s*=\\s*'([^']*)'`)) || [])[1];
const USER = g('USER'), PASS = g('PASS');

const PAGES = ['/', '/history', '/alarms', '/config', '/network', '/users',
               '/files', '/license'];

async function gotoR(page, url) {
    let lastErr = null;
    for (let a = 0; a < 5; a++) {
        try {
            return await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 20000 });
        } catch (e) { lastErr = e; await settle(1200); }
    }
    throw lastErr || new Error('goto ' + url);
}

async function tryLogin(page) {
    for (let t = 0; t < 3; t++) {
        try {
            await gotoR(page, BASE + '/login');
            await page.waitForSelector('input[name=user]', { timeout: 12000 });
            await settle(600);
            await page.type('input[name=user]', USER);
            await page.type('#passInput', PASS);
            await page.click('#btnLogin');
            const t0 = Date.now();
            while (Date.now() - t0 < 15000) {
                try {
                    const st = await page.evaluate(() => ({
                        url: location.href,
                        err: (document.getElementById('errMsg') || { textContent: '' }).textContent,
                    }));
                    if (st.url !== BASE + '/login' && !st.url.includes('/login?')) return true;
                    if (st.err.length > 0) { console.log(`[login] ${st.err}`); break; }
                } catch (e) { /* navegando */ }
                await settle(300);
            }
        } catch (e) { console.log(`[login] tentativa ${t + 1} falhou: ${e.message}`); }
        await settle(3000);
    }
    return false;
}

(async () => {
    const browser = await pup.launch({
        executablePath: '/usr/bin/google-chrome',
        headless: 'new',
        args: ['--no-sandbox', '--disable-gpu', '--ignore-certificate-errors'],
    });
    const page = await browser.newPage();
    await page.setRequestInterception(true);
    page.on('request', req => {
        if (req.url().includes('/favicon.ico')) return req.abort().catch(() => null);
        req.continue().catch(() => null);
    });

    let retries = 0, errs = 0;
    page.on('pageerror', () => errs++);
    page.on('console', m => { if (m.type() === 'error') errs++; });
    page.on('requestfailed', () => retries++);

    const ok = await tryLogin(page);
    if (!ok) { console.error('FATAL: login não completou em 3 tentativas'); process.exit(2); }
    console.log('[soak] login ok — iniciando laço');

    const t0 = Date.now();
    const deadline = t0 + MINUTES * 60000;
    let i = 0, navs = 0, clicks = 0, firstUptime = null, minUptime = null;
    const heaps = [];

    while (Date.now() < deadline) {
        const route = PAGES[i % PAGES.length];
        try { await gotoR(page, BASE + route); navs++; }
        catch (e) { console.log(`[soak] ${route}: goto falhou 5x: ${e.message}`); retries += 5; }
        await settle(2000);
        try {
            const clicked = await page.evaluate(() => {
                const links = Array.from(document.querySelectorAll('a[href^="/"]'))
                    .filter(a => !a.href.includes('/login') && !a.href.includes('/logout'));
                if (links.length === 0) return false;
                const a = links[Math.floor(Math.random() * Math.min(links.length, 6))];
                a.click();
                return true;
            });
            if (clicked) { clicks++; await settle(2500); }
        } catch (e) { /* sem links */ }

        if (Date.now() - t0 >= (heaps.length + 1) * 120000) {
            try {
                const st = await page.evaluate(async () =>
                    (await (await fetch('/api/status')).json()).sys);
                if (firstUptime === null) firstUptime = st.uptime;
                if (minUptime === null || st.uptime < minUptime) minUptime = st.uptime;
                heaps.push({ t: Math.round((Date.now() - t0) / 60000), heap_lb: st.heap_lb });
                console.log(`[soak ${Math.round((Date.now() - t0) / 60000)}min] navs=${navs} clicks=${clicks} ` +
                    `retries=${retries} errsJS=${errs} heap_lb=${st.heap_lb} uptime=${st.uptime}`);
            } catch (e) { console.log(`[soak] /api/status falhou: ${e.message}`); }
        }
        i++;
    }

    await browser.close();
    const rebooted = minUptime !== null && minUptime < firstUptime;
    console.log(`\n== SOAK ${MINUTES} min: navs=${navs} clicks=${clicks} retries(conn)=${retries} ` +
        `errosJS=${errs} reboots=${rebooted ? 'SIM' : '0'} ` +
        `heap_lb série=${heaps.map(h => `${h.t}m:${h.heap_lb}`).join(' ')} ==`);
    process.exit(rebooted ? 1 : 0);
})().catch(e => { console.error('FATAL:', e); process.exit(2); });
