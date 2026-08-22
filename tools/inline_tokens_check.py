#!/usr/bin/env python3
"""
inline_tokens_check.py — v2.3.2 white-flash fix gate (PLANO-VALIDACAO §5.7).

Every page embeds the theme tokens inline in a <style> block so the first
paint is dark before /style.css arrives (commit 1356df2). This tool asserts
the inline blocks carry the SAME VALUES as the canonical sources served by
the device:

  * dark  : :root            of /style.css
  * light : :root.theme-light of /lang.js (injected sync in head)

Usage:
    python3 tools/inline_tokens_check.py [--host IP] [--https]

Credentials: SIMUT_WEB_USER / SIMUT_WEB_PASS env, or scratchpad/rig_secrets.py.

Exit code: 0 = 0 diff em todas as páginas; 1 = divergência.
"""
import argparse
import hashlib
import os
import re
import sys

import requests

PAGES = ['/', '/history', '/alarms', '/config', '/network', '/users',
         '/files', '/license', '/login']

ROOT_BLOCK = re.compile(r':root\s*\{([^}]*)\}', re.S)
THEMELIGHT_BLOCK = re.compile(r':root\.theme-light\s*\{([^}]*)\}', re.S)
LIGHT_BLOCK = re.compile(r'html\.light\s*\{([^}]*)\}', re.S)
STYLE_TAGS = re.compile(r'<style[^>]*>(.*?)</style>', re.S | re.I)
# decl termina em ';', no fecho do bloco (lang.js: 'color-scheme:light}') ou
# no fim do grupo capturado (mesmo caso, com o '}' já fora do grupo)
DECL = re.compile(r'([\w-]+)\s*:\s*([^;]+?)\s*(?:;|(?=\})|$)', re.M)


def parse_decls(block):
    out = {}
    for m in DECL.finditer(block):
        out[m.group(1).strip()] = m.group(2).strip()
    return out


def parse_decls(block):
    out = {}
    for m in DECL.finditer(block):
        out[m.group(1).strip()] = m.group(2).strip()
    return out


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
    for k, v in sorted(got.items()):
        if k not in ref:
            diffs.append(f'{name} {k}: só na página ({v})')
        elif ref[k] != v:
            diffs.append(f'{name} {k}: página={v} ref={ref[k]}')
    return diffs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='192.168.3.24')
    ap.add_argument('--https', action='store_true')
    args = ap.parse_args()

    scheme = 'https' if args.https else 'http'
    base = f'{scheme}://{args.host}'

    s = requests.Session()
    login(s, base)

    css = s.get(base + '/style.css', timeout=15)
    css.raise_for_status()
    js = s.get(base + '/lang.js', timeout=15)
    js.raise_for_status()

    m = ROOT_BLOCK.search(css.text)
    lm = THEMELIGHT_BLOCK.search(js.text)
    if not m:
        sys.exit('style.css sem :root — não consigo comparar')
    if not lm:
        sys.exit('lang.js sem :root.theme-light — não consigo comparar')
    ref_root = parse_decls(m.group(1))
    ref_light = parse_decls(lm.group(1))
    print(f'ref style.css :root = {len(ref_root)} tokens')
    print(f'ref lang.js theme-light = {len(ref_light)} tokens')

    fails = 0
    for page in PAGES:
        r = s.get(base + page, timeout=15)
        r.raise_for_status()
        tags = STYLE_TAGS.findall(r.text)
        page_root = {}
        page_light = {}
        for tag in tags:
            mm = ROOT_BLOCK.search(tag)
            if mm:
                page_root.update(parse_decls(mm.group(1)))
            ll = LIGHT_BLOCK.search(tag)
            if ll:
                page_light.update(parse_decls(ll.group(1)))
        diffs = []
        if not page_root:
            diffs.append('nenhum bloco :root inline encontrado')
        diffs += cmp_tokens('dark', page_root, ref_root)
        if page_light:
            # claro inline existe só em páginas públicas (/login); nas
            # autenticadas o tema claro vem do lang.js (por design).
            diffs += cmp_tokens('light', page_light, ref_light)
            light_note = (f'light inline {len(page_light)}/{len(ref_light)} '
                          'tokens idênticos ao lang.js')
        else:
            light_note = 'light via lang.js (por design)'
        if diffs:
            fails += 1
            print(f'[FAIL] {page}')
            for d in diffs:
                print(f'       {d}')
        else:
            print(f'[PASS] {page} — dark {len(page_root)}/'
                  f'{len(ref_root)} tokens idênticos ao style.css; {light_note}')
    print(f'-- {len(PAGES)} páginas, {fails} com divergência --')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
