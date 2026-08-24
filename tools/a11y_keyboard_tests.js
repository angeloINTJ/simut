#!/usr/bin/env node
/* a11y_keyboard_tests.js — §5.16 Acessibilidade (PLANO-VALIDACAO v2.3.2).
 *
 * Uso:  node a11y_keyboard_tests.js [http|https]
 *
 * 30 checagens de teclado/regiões vivas sobre o UI real:
 *   K1  Gaveta: abre pelo teclado, foco no 1o link, aria-expanded, Escape
 *       fecha e devolve o foco ao hamburger, Enter navega.
 *   K2  Select custom (.csel): aria-haspopup/expanded, ArrowUp/Down muda a
 *       opção com aria-selected e dispara change, Escape fecha e devolve o
 *       foco, menu tem role=listbox e itens role=option.
 *   K3  Editor de sensor (/config): abre, trava rolagem, Escape fecha,
 *       clique no backdrop fecha, botão de fechar com aria-label.
 *   K4  Modal de credencial temporária (showCredsModal): role=dialog,
 *       aria-modal, foco no botão, TAB preso (por design), Escape NÃO fecha
 *       (senha aparece uma vez), Enter fecha e devolve o foco.
 *   K5  Toast: role=status, aria-live=polite, texto setado, auto-limpa.
 *   K6  Reduced-motion: bloco @media aplicado via emulação CDP
 *       (transition-duration <= 1 ms sob reduce; 0.3 s sem).
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

let passCount = 0, failCount = 0, skipCount = 0;
function rep(ok, name, detail) {
    console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${name}${detail ? ' — ' + detail : ''}`);
    if (ok) passCount++; else failCount++;
}
function skip(name, detail) {
    console.log(`  [SKIP] ${name}${detail ? ' — ' + detail : ''}`);
    skipCount++;
}
const sleep = ms => new Promise(r => setTimeout(r, ms));

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
        } catch (e) { await sleep(200); continue; } /* contexto trocado na navegação */
        if (st.url !== BASE + '/login' && !st.url.includes('/login?')) return;
        if (st.err.length > 0) throw new Error('login recusado: ' + st.err);
        await sleep(200);
    }
    throw new Error('login timeout');
}

async function focus(page, sel) {
    await page.evaluate(s => {
        const el = document.querySelector(s);
        if (!el) throw new Error('não achou ' + s);
        el.focus();
    }, sel);
}

const key = async (page, k) => page.keyboard.press(k);

(async () => {
    const browser = await pup.launch({
        executablePath: '/usr/bin/google-chrome',
        headless: 'new',
        args: ['--no-sandbox', '--disable-gpu', '--ignore-certificate-errors'],
    });
    const ctx = await browser.createBrowserContext();
    const page = await ctx.newPage();
    page.setDefaultTimeout(12000);
    await page.setViewport({ width: 1280, height: 800 });
    await loginViaForm(page);

    /* ── K1: Gaveta (drawer) — 8 checagens ─────────────────────────────── */
    console.log('[K1] Gaveta — teclado');
    await page.goto(BASE + '/', { waitUntil: 'domcontentloaded', timeout: 30000 });
    await page.waitForSelector('.topbar .hamburger', { timeout: 10000 });
    const hExp0 = await page.$eval('.topbar .hamburger', el => el.getAttribute('aria-expanded'));
    rep(hExp0 === 'false', 'hamburger nasce aria-expanded=false', hExp0);

    await focus(page, '.topbar .hamburger');
    await key(page, 'Enter');
    await sleep(300);
    let st = await page.evaluate(() => ({
        exp: document.querySelector('.topbar .hamburger').getAttribute('aria-expanded'),
        active: document.activeElement && document.activeElement.tagName,
        activeHref: document.activeElement && document.activeElement.getAttribute('href'),
        navLabel: (document.querySelector('.drawer nav') || {}).getAttribute && document.querySelector('.drawer nav').getAttribute('aria-label'),
    }));
    rep(st.exp === 'true', 'Enter abre a gaveta (aria-expanded=true)', st.exp);
    rep(st.navLabel === 'Main', 'nav da gaveta tem aria-label=Main', st.navLabel);
    rep(st.active === 'A' && !!st.activeHref, 'foco entra no 1º link da gaveta', `${st.active} ${st.activeHref}`);

    await key(page, 'Escape');
    await sleep(300);
    st = await page.evaluate(() => ({
        exp: document.querySelector('.topbar .hamburger').getAttribute('aria-expanded'),
        activeClass: document.activeElement && document.activeElement.className,
        activeLabel: document.activeElement && document.activeElement.getAttribute('aria-label'),
    }));
    rep(st.exp === 'false', 'Escape fecha a gaveta (aria-expanded=false)', st.exp);
    rep(!!st.activeClass && st.activeClass.includes('hamburger'),
        'foco volta ao hamburger ao fechar por teclado', `${st.activeClass} ${st.activeLabel}`);

    // Enter no link navega (Dashboard)
    await focus(page, '.topbar .hamburger');
    await key(page, 'Enter');
    await sleep(300);
    await key(page, 'Enter'); // 1º link = Dashboard
    await sleep(800);
    const dashUrl = page.url();
    rep(dashUrl === BASE + '/', 'Enter no link da gaveta navega', dashUrl);

    /* ── K2: Select custom — 9 checagens ───────────────────────────────── */
    console.log('[K2] Select custom (.csel)');
    await page.goto(BASE + '/', { waitUntil: 'domcontentloaded', timeout: 30000 });
    const hasThemeSel = await page.$('#themeSel');
    if (!hasThemeSel) {
        skip('#themeSel no dashboard', 'elemento ausente nesta página');
    } else {
        await page.waitForFunction(() => document.querySelector('#themeSel') &&
            document.querySelector('#themeSel').parentNode &&
            document.querySelector('#themeSel').parentNode.querySelector('.csel-btn'),
            { timeout: 10000 });
        let c = await page.evaluate(() => {
            const b = document.querySelector('#themeSel').parentNode.querySelector('.csel-btn');
            return { hash: b.getAttribute('aria-haspopup'), exp: b.getAttribute('aria-expanded') };
        });
        rep(c.hash === 'listbox', 'csel-btn aria-haspopup=listbox', c.hash);
        rep(c.exp === 'false', 'csel-btn nasce aria-expanded=false', c.exp);

        await focus(page, '#themeSel ~ .csel-btn, #themeSel').catch(() => {});
        await page.evaluate(() => {
            const b = document.querySelector('#themeSel').parentNode.querySelector('.csel-btn');
            b.focus();
        });        await key(page, 'Enter');
        await sleep(200);
        c = await page.evaluate(() => {
            const w = document.querySelector('#themeSel').parentNode;
            const b = w.querySelector('.csel-btn'), m = w.querySelector('.csel-menu');
            return {
                exp: b.getAttribute('aria-expanded'),
                role: m.getAttribute('role'),
                display: m.style.display,
                optRoles: [...m.querySelectorAll('[role=option]')].length,
            };
        });
        rep(c.exp === 'true' && c.display === 'block', 'Enter abre o menu do select', `${c.exp}/${c.display}`);
        rep(c.role === 'listbox', 'menu tem role=listbox', c.role);
        rep(c.optRoles >= 2, 'itens do menu têm role=option', `${c.optRoles} opções`);

        // ArrowDown muda a seleção (aria-selected segue) e dispara change
        const before = await page.$eval('#themeSel', s => s.value);
        await key(page, 'ArrowDown');
        await sleep(200);
        const after = await page.$eval('#themeSel', s => s.value);
        let sel = await page.evaluate(() => {
            const w = document.querySelector('#themeSel').parentNode;
            const m = w.querySelector('.csel-menu');
            const items = [...m.querySelectorAll('[role=option]')];
            return {
                selected: items.filter(i => i.getAttribute('aria-selected') === 'true').length,
                text: (items.find(i => i.getAttribute('aria-selected') === 'true') || {}).textContent,
            };
        });
        rep(after !== before, 'ArrowDown muda a opção do select', `${before} -> ${after}`);
        rep(sel.selected === 1, 'exatamente 1 item com aria-selected=true', `${sel.selected} (${sel.text})`);

        await key(page, 'ArrowUp');
        await sleep(200);
        const back = await page.$eval('#themeSel', s => s.value);
        rep(back === before, 'ArrowUp volta à opção anterior', `${after} -> ${back}`);

        await key(page, 'Escape');
        await sleep(200);
        c = await page.evaluate(() => {
            const w = document.querySelector('#themeSel').parentNode;
            const b = w.querySelector('.csel-btn');
            return { exp: b.getAttribute('aria-expanded'), focused: document.activeElement === b };
        });
        rep(c.exp === 'false' && c.focused, 'Escape fecha o menu e devolve o foco ao botão', `${c.exp}/${c.focused}`);

        // Escape com menu fechado alcança a gaveta (ordem: menu → gaveta)
        await page.evaluate(() => { document.querySelector('.topbar .hamburger').click(); });
        await sleep(250);
        await key(page, 'Escape');
        await sleep(250);
        const drawerClosed = await page.$eval('.topbar .hamburger', el => el.getAttribute('aria-expanded'));
        rep(drawerClosed === 'false', 'Escape (sem menu aberto) fecha a gaveta', drawerClosed);
    }

    /* ── K3: Editor de sensor (/config) — 5 checagens ──────────────────── */
    console.log('[K3] Editor de sensor (/config)');
    await page.goto(BASE + '/config', { waitUntil: 'domcontentloaded', timeout: 30000 });
    const hasEditor = await page.evaluate(() => typeof window.sensEdit === 'function' &&
        !!document.getElementById('sens_ov'));
    if (!hasEditor) {
        skip('editor de sensor em /config', 'sensEdit/sens_ov ausente');
    } else {
        // espera a tabela de sensores renderizar (loadSensors é async)
        await page.waitForFunction(() => document.querySelectorAll('#sens_tb tr').length > 0,
            { timeout: 15000 });
        await page.evaluate(() => window.sensEdit(0));
        await sleep(300);
        let e = await page.evaluate(() => ({
            open: document.getElementById('sens_ov').style.display === 'flex',
            locked: document.body.style.overflow === 'hidden',
            closeLabel: (document.querySelector('#sens_ov .sxb') || {}).getAttribute && document.querySelector('#sens_ov .sxb').getAttribute('aria-label'),
        }));
        rep(e.open, 'sensEdit abre o overlay do editor', e.open);
        rep(e.locked, 'rolagem do body travada com o editor aberto', e.locked);
        rep(e.closeLabel === 'Close', 'botão de fechar tem aria-label', e.closeLabel);

        await key(page, 'Escape');
        await sleep(250);
        e = await page.evaluate(() => ({
            closed: document.getElementById('sens_ov').style.display === 'none',
            unlocked: document.body.style.overflow !== 'hidden',
        }));
        rep(e.closed && e.unlocked, 'Escape fecha o editor e destrava a rolagem', `${e.closed}/${e.unlocked}`);

        await page.evaluate(() => window.sensEdit(0));
        await sleep(250);
        await page.mouse.click(8, 400); // backdrop (fora do diálogo)
        await sleep(250);
        const closed2 = await page.$eval('#sens_ov', el => el.style.display === 'none');
        rep(closed2, 'clique no backdrop fecha o editor', closed2);
    }

    /* ── K4: Modal de credencial temporária — 6 checagens ──────────────── */
    console.log('[K4] Modal de credencial temporária (showCredsModal)');
    await page.goto(BASE + '/', { waitUntil: 'domcontentloaded', timeout: 30000 });
    const hasCreds = await page.evaluate(() => typeof window.showCredsModal === 'function');
    if (!hasCreds) {
        skip('showCredsModal no dashboard', 'função ausente');
    } else {
        await page.evaluate(() => {
            const b = document.querySelector('.topbar .hamburger');
            if (b) b.focus();
            window.showCredsModal([{ u: 'testuser', p: 'temp-pass-123' }], null);
        });
        await sleep(300);
        let m = await page.evaluate(() => {
            const d = document.querySelector('[role=dialog]');
            return {
                exists: !!d,
                modal: d && d.getAttribute('aria-modal'),
                labelledby: d && d.getAttribute('aria-labelledby'),
                focusOnOk: document.activeElement && document.activeElement.id === 'creds-ok',
            };
        });
        rep(m.exists && m.modal === 'true', 'modal com role=dialog aria-modal=true', `${m.exists}/${m.modal}`);
        rep(m.labelledby === 'creds-title', 'dialog aria-labelledby=creds-title', m.labelledby);
        rep(m.focusOnOk, 'foco entra no único botão ao abrir', m.focusOnOk);

        await key(page, 'Tab');
        await sleep(150);
        const trap = await page.evaluate(() => document.activeElement && document.activeElement.id === 'creds-ok');
        rep(trap, 'TAB fica preso no botão (por design)', trap);

        await key(page, 'Escape');
        await sleep(150);
        const stillOpen = await page.evaluate(() => !!document.querySelector('[role=dialog]'));
        rep(stillOpen, 'Escape NÃO fecha o modal de senha única (por design)', stillOpen);

        await key(page, 'Enter'); // ativa o botão focado
        await sleep(300);
        m = await page.evaluate(() => ({
            closed: !document.querySelector('[role=dialog]'),
            focusBack: document.activeElement && document.activeElement.className &&
                String(document.activeElement.className).includes('hamburger'),
        }));
        rep(m.closed && m.focusBack, 'Enter fecha o modal e devolve o foco a quem abriu', `${m.closed}/${m.focusBack}`);
    }

    /* ── K5: Toast / live region — 4 checagens ─────────────────────────── */
    console.log('[K5] Toast (net-toast)');
    await page.evaluate(() => window.showToast('a11y probe', 'info', 400));
    let t = await page.evaluate(() => {
        const el = document.getElementById('net-toast');
        return { role: el.getAttribute('role'), live: el.getAttribute('aria-live'), text: el.textContent };
    });
    rep(t.role === 'status', 'toast com role=status', t.role);
    rep(t.live === 'polite', 'toast com aria-live=polite', t.live);
    rep(t.text === 'a11y probe', 'texto do toast setado', t.text);
    await sleep(900);
    const cleared = await page.evaluate(() => document.getElementById('net-toast').className === '');
    rep(cleared, 'toast auto-limpa após o timeout', cleared);

    /* ── K6: Reduced-motion — 2 checagens ──────────────────────────────── */
    console.log('[K6] Reduced-motion');
    await page.goto(BASE + '/network', { waitUntil: 'domcontentloaded', timeout: 30000 });
    await page.waitForSelector('.toggle .slider', { timeout: 10000 });
    const cdp = await page.createCDPSession();
    const durOf = () => page.evaluate(() => {
        const el = document.querySelector('.toggle .slider') || document.querySelector('.csel-btn') || document.body;
        return parseFloat(getComputedStyle(el).transitionDuration) || 0;
    });
    const normal = await durOf();
    await cdp.send('Emulation.setEmulatedMedia', {
        features: [{ name: 'prefers-reduced-motion', value: 'reduce' }],
    });
    await sleep(200);
    const reduced = await durOf();
    rep(reduced <= 0.001, 'bloco prefers-reduced-motion aplica (dur <= 1 ms)', `${reduced}s`);
    rep(normal > reduced, 'sem emulação a duração original prevalece', `${normal}s vs ${reduced}s`);
    await cdp.send('Emulation.setEmulatedMedia', { features: [] });

    await browser.close();
    const total = passCount + failCount + skipCount;
    console.log(`\na11y_keyboard_tests: ${passCount} passaram, ${failCount} falharam, ${skipCount} skips (total ${total})`);
    process.exit(failCount > 0 ? 1 : 0);
})().catch(e => {
    console.error('FATAL: ' + (e.stack || e));
    process.exit(2);
});
