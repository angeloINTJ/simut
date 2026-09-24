#!/usr/bin/env python3
"""
inline_tokens_check.py — portão das cópias dos tokens de cor (Ângulo).

Os dezessete papéis de cor dos dois temas vivem numa cópia só, injetada pelo
/lang.js antes da primeira pintura. Uma página não carrega o /lang.js — /login,
que vem antes da sessão e também atende /force_chpass (mesmo blob, modo
forçado escolhido pelo pathname desde 2026-09-24) — e por isso leva a mesma
tabela inline. "Mudou lá, mude aqui" é a regra que este portão faz valer: ele
compara, declaração a declaração, o bloco servido no /lang.js com o bloco
inline da página pré-sessão.

O segundo invariante é o contrário: nenhuma página AUTENTICADA pode carregar
um bloco `:root{` inline. Até a 2.4.5 cada uma trazia uma cópia dos tokens
escuros ("M1 anti-piscada"), nove cópias que divergiam em silêncio; a
reforma as retirou, e uma que volte é regressão.

Usage:
    python3 tools/inline_tokens_check.py [--host IP[:porta]]

Credentials: SIMUT_WEB_USER / SIMUT_WEB_PASS env, or scratchpad/rig_secrets.py.

Exit code: 0 = tudo idêntico; 1 = divergência ou cópia onde não devia.
"""
import argparse
import hashlib
import os
import re
import sys

import requests

PRE_SESSION = ['/login']  # /force_chpass serves this same blob (forced mode picked from the pathname)
AUTHENTICATED = ['/', '/history', '/alarms', '/telemetry', '/config',
                 '/network', '/users', '/files', '/license']

# ':root{...}' e ':root[data-theme=claro]{...}' — o bloco escuro e o claro.
DARK_BLOCK = re.compile(r':root\s*\{([^}]*)\}', re.S)
LIGHT_BLOCK = re.compile(r':root\[data-theme=claro\]\s*\{([^}]*)\}', re.S)
STYLE_TAGS = re.compile(r'<style[^>]*>(.*?)</style>', re.S | re.I)
DECL = re.compile(r'([\w-]+)\s*:\s*([^;]+?)\s*(?:;|(?=\})|$)', re.M)


def parse_decls(block):
    return {m.group(1).strip(): m.group(2).strip() for m in DECL.finditer(block)}


def login(s, base):
    n = s.get(base + '/api/login_init', timeout=10).json()['nonce']
    user = os.environ.get('SIMUT_WEB_USER', 'admin')
    pw = os.environ.get('SIMUT_WEB_PASS')
    if pw is None:
        sys.path.insert(0, 'scratchpad')
        import rig_secrets  # noqa: E402  (local bench creds, gitignored)
        user, pw = rig_secrets.USER, rig_secrets.PASS
    r = s.post(base + '/api/login',
               data={'user': user,
                     'pass': hashlib.sha256(pw.encode('latin-1')).hexdigest(),
                     'nonce': n}, timeout=10)
    if r.status_code != 200:
        sys.exit(f'login falhou: HTTP {r.status_code} {r.text[:120]}')


def cmp_tokens(name, got, ref):
    diffs = []
    for k, v in sorted(ref.items()):
        if k not in got:
            diffs.append(f'{name} {k}: falta na página (lang.js={v})')
        elif got[k] != v:
            diffs.append(f'{name} {k}: página={got[k]} lang.js={v}')
    for k in sorted(set(got) - set(ref)):
        diffs.append(f'{name} {k}: só na página ({got[k]})')
    return diffs


def blocks_in(html):
    dark, light = {}, {}
    for tag in STYLE_TAGS.findall(html):
        m = DARK_BLOCK.search(tag)
        if m:
            dark.update(parse_decls(m.group(1)))
        m = LIGHT_BLOCK.search(tag)
        if m:
            light.update(parse_decls(m.group(1)))
    return dark, light


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='192.168.3.24')
    ap.add_argument('--https', action='store_true')
    args = ap.parse_args()
    base = ('https' if args.https else 'http') + '://' + args.host

    s = requests.Session()
    js = s.get(base + '/lang.js', timeout=15)
    js.raise_for_status()
    dm, lm = DARK_BLOCK.search(js.text), LIGHT_BLOCK.search(js.text)
    if not dm or not lm:
        sys.exit('lang.js sem o bloco de tokens dos dois temas — não consigo comparar')
    ref_dark, ref_light = parse_decls(dm.group(1)), parse_decls(lm.group(1))
    print(f'ref lang.js: escuro {len(ref_dark)} · claro {len(ref_light)} declarações')

    fails = 0
    for page in PRE_SESSION:
        r = s.get(base + page, timeout=15, allow_redirects=False)
        if r.status_code != 200:
            print(f'[SKIP] {page} — HTTP {r.status_code} (só é servida quando a conta pede senha nova)')
            continue
        dark, light = blocks_in(r.text)
        diffs = []
        if not dark or not light:
            diffs.append('sem a tabela inline dos dois temas')
        diffs += cmp_tokens('escuro', dark, ref_dark) + cmp_tokens('claro', light, ref_light)
        if diffs:
            fails += 1
            print(f'[FAIL] {page}')
            for d in diffs:
                print(f'       {d}')
        else:
            print(f'[PASS] {page} — {len(dark)} + {len(light)} declarações idênticas ao lang.js')

    login(s, base)
    for page in AUTHENTICATED:
        r = s.get(base + page, timeout=15)
        r.raise_for_status()
        dark, light = blocks_in(r.text)
        if dark or light:
            fails += 1
            print(f'[FAIL] {page} — carrega tokens inline ({len(dark)} + {len(light)}); '
                  'a cópia é do lang.js, não da página')
        else:
            print(f'[PASS] {page} — sem cópia inline (tokens do lang.js)')
    print(f'-- {fails} falhas --')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
