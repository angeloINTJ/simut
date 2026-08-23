#!/usr/bin/env node
/* offline_dns_tests.js — §5.17 Visual web: sem dependência externa
 * (PLANO-VALIDACAO v2.3.2).
 *
 * Uso:  node offline_dns_tests.js [http|https]
 *
 * Deruba a resolução DNS externa no nível do navegador
 * (--host-resolver-rules: qualquer host fora do dispositivo resolve para
 * ~NOTFOUND) e percorre as 12 rotas autenticadas. Limites do plano:
 *   33 requests no total da navegação, 0 request externa, 0 erro JS,
 *   e o gráfico do /history desenha offline (canvas com pixels pintados).
 *
 * Credenciais: SIMUT_WEB_USER/SIMUT_WEB_PASS ou scratchpad/rig_secrets.py.
 */
'use strict';
const fs = require('fs');
const path = require('path');
const pup = require('/home/angelo/Documentos/simut/scratchpad/h5g_20260818/node_modules/puppeteer-core');

const HOST = '192.168.3.24';
const SCHEME = (process.argv[2] || 'http').toLowerCase();
const BASE = `${SCHEME}://${HOST}`;

const PAGES = ['/', '/history', '/alarms', '/config', '/network', '/users',
               '/files', '/license', '/login'];

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
const sleep = ms => new Promise(r => setTimeout(r, ms));

const requests = [];        // {url, host, ok}
const failures = [];        // requestfailed / respostas >= 400
const jsErrors = [];        // pageerror / console.error

function watch(page) {
    page.on('request', r => requests.push({ url: r.url(), host: (() => {
        try { return new URL(r.url()).host; } catch (e) { return '(data)'; }
    })() }));
    page.on('requestfailed', r => failures.push('requestfailed: ' + r.url()));
    page.on('response', r => { if (r.status() >= 400) failures.push(`HTTP ${r.status()}: ${r.url()}`); });
    page.on('pageerror', e => jsErrors.push('pageerror: ' + (e.stack || e.message)));
    page.on('console', m => { if (m.type() === 'error') jsErrors.push('console: ' + m.text()); });
}

async function loginViaForm(page) {
    await page.goto(BASE + '/login', { waitUntil: 'domcontentloaded', timeout: 30000 });
    await page.waitForSelector('input[name=user]', { timeout: 10000 });
    await new Promise(r => setTimeout(r, 400));
    await page.type('input[name=user]', USER);
    await page.type('#passInput', PASS);
    await page.click('#btnLogin');
    const t0 = Date.now();
    while (Date.now() - t0 < 15000) {
        let st;
        try {
            st = await page.evaluate(() => ({
                url: location.href,
                err: (document.getElementById('errMsg') || { textContent: '' }).textContent,
            }));
        } catch (e) { await sleep(200); continue; }
        if (st.url !== BASE + '/login' && !st.url.includes('/login?')) return;
        if (st.err.length > 0) throw new Error('login recusado: ' + st.err);
        await sleep(200);
    }
    throw new Error('login timeout');
}

async function graphPainted(page) {
    /* /history: o mini-chart embutido desenha num canvas. Conta pixels que
     * não são o fundo (qualquer alpha > 0) — só o canvas do gráfico. */
    return page.evaluate(() => {
        const c = document.querySelector('canvas');
        if (!c) return { ok: false, reason: 'sem canvas' };
        const ctx = c.getContext('2d');
        if (!ctx) return { ok: false, reason: 'sem contexto 2d' };
        const d = ctx.getImageData(0, 0, c.width, c.height).data;
        let painted = 0;
        for (let i = 3; i < d.length; i += 4) if (d[i] > 0) painted++;
        return { ok: painted > 0, painted, total: c.width * c.height };
    });
}

(async () => {
    const browser = await pup.launch({
        executablePath: '/usr/bin/google-chrome',
        headless: 'new',
        args: [
            '--no-sandbox', '--disable-gpu', '--ignore-certificate-errors',
            `--host-resolver-rules=MAP * ~NOTFOUND , EXCLUDE ${HOST}`,
        ],
    });
    const ctx = await browser.createBrowserContext();
    const page = await ctx.newPage();
    page.setDefaultTimeout(20000);
    watch(page);
    await loginViaForm(page);

    console.log('[O1] 12 rotas autenticadas com DNS externo morto');
    for (const p of PAGES) {
        await page.goto(BASE + p, { waitUntil: 'domcontentloaded', timeout: 30000 });
        await sleep(350); /* deixa o poll inicial e o lang.js assentarem */
    }

    // Volta ao /history e espera o gráfico terminar de desenhar.
    await page.goto(BASE + '/history', { waitUntil: 'domcontentloaded', timeout: 30000 });
    await page.waitForSelector('canvas', { timeout: 15000 });
    await sleep(2500);
    const g = await graphPainted(page);
    rep(g.ok, 'gráfico do /history desenha offline', `${g.painted}/${g.total} px pintados`);

    const total = requests.length;
    const external = requests.filter(r => {
        if (r.host === '(data)') return false;
        return r.host !== HOST && r.host !== '127.0.0.1' && r.host !== 'localhost';
    });
    rep(external.length === 0, '0 request externa', external.map(r => r.url).join(' ') || 'nenhuma');
    rep(failures.length === 0, '0 requestfailed / resposta >= 400', failures.join(' ') || 'nenhuma');
    rep(jsErrors.length === 0, '0 erro JS', jsErrors.join(' ') || 'nenhum');
    rep(total === 33, '33 requests no total da navegação', `${total} requests`);

    if (total !== 33) {
        console.log('  [INFO] distribuição por página:');
        const counts = {};
        for (const p of PAGES) counts[p] = requests.filter(r => r.url.includes(p.replace('/', ''))).length;
        console.log('  ' + JSON.stringify(counts));
        console.log('  [INFO] requests observadas:');
        for (const r of requests) console.log('    ' + r.host + ' ' + r.url);
    }

    await browser.close();
    console.log(`\noffline_dns_tests: ${passCount} passaram, ${failCount} falharam`);
    process.exit(failCount > 0 ? 1 : 0);
})().catch(e => {
    console.error('FATAL: ' + (e.stack || e));
    process.exit(2);
});
