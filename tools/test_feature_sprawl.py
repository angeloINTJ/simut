#!/usr/bin/env python3
"""test_feature_sprawl.py -- what tools/check_feature_sprawl.py counts, on the host (S10c, the Wi-Fi status bar fix).

The gate counts, per SIMUT_ macro and per file, the places in src/ that test the macro, and holds them to the watermark in tools/feature_sprawl.json. Until 2026-10-30 a "place" was a preprocessor line (`#if`, `#ifdef`,
`#ifndef`, `#elif`), so a file could branch on a feature with `if constexpr ( SIMUT_X )` and the gate saw nothing: DisplayManager_UiA.cpp's network glyph did exactly that, for a day, to keep the file at one `#if` on
the network class. The gate counts that line as well now, and this holds how (the gate had no test before):

  1. What counts, on temporary trees: the four preprocessor lines (the control), and `if constexpr ( SIMUT_X )` (with and without spaces, after `} else`, after `else`), each macro of a condition once; an `#if` and
     an `if constexpr` on one macro are two sites.
  2. What does not: an `if constexpr` that tests no SIMUT_ macro, a comment line, the files the gate skips, a file that is not source.
  3. The verdict: a tree with one site more than its watermark exits 1 and names the file; with the watermark raised, 0.
  4. The real tree: the glue's `if constexpr ( SIMUT_NET_WIFI )` is seen by the real gate.
  5. The mutants: the gate's source edited one rule at a time (the `if constexpr` rule not used, `else` not accepted, `} else` not accepted, the line anchor dropped so that a comment counts, every macro of a
     line counted and not each once); each must fail a case, and an edit whose anchor is not found exactly once fails this script.

Usage: python3 tools/test_feature_sprawl.py   exit 0 when all hold, 1 when not.
"""
import contextlib
import importlib.util
import io
import json
import pathlib
import re
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
GATE = HERE / 'check_feature_sprawl.py'

CONTA = [0]
FALHAS = []


def confere(cond, msg):
    CONTA[0] += 1
    if not cond:
        FALHAS.append(msg)
        print('FAIL', msg)
    return bool(cond)


def carrega(caminho, nome):
    spec = importlib.util.spec_from_file_location(nome, str(caminho))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def arvore(raiz, arquivos):
    for rel, texto in arquivos.items():
        p = raiz / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(texto, encoding='utf-8')


def conta(mod, arquivos):
    with tempfile.TemporaryDirectory() as t:
        raiz = pathlib.Path(t)
        arvore(raiz, arquivos)
        mod.SRC = str(raiz)
        return mod.scan()


def veredito(mod, arquivos, marca):
    """(exit code, output) of the gate's own main( ) on a temporary tree against a watermark."""
    with tempfile.TemporaryDirectory() as t:
        raiz = pathlib.Path(t)
        arvore(raiz / 'src', arquivos)
        (raiz / 'marca.json').write_text(json.dumps(marca), encoding='utf-8')
        mod.SRC, mod.BASELINE = str(raiz / 'src'), str(raiz / 'marca.json')
        antes, sys.argv = sys.argv, ['check_feature_sprawl.py']
        saida = io.StringIO()
        try:
            with contextlib.redirect_stdout(saida):
                rc = mod.main()
        finally:
            sys.argv = antes
        return rc, saida.getvalue()


# (name, {file: text}, what the gate must count)
CASOS = [
    ('#if counts (the control: the gate\'s first rule)', {'a.cpp': '#if SIMUT_A\nint x;\n#endif\n'}, {'SIMUT_A': {'a.cpp': 1}}),
    ('#ifdef, #elif and #ifndef count, each macro once', {'a.cpp': '#ifdef SIMUT_A\n#elif SIMUT_B\n#ifndef SIMUT_C\n#endif\n#endif\n#endif\n'},
     {'SIMUT_A': {'a.cpp': 1}, 'SIMUT_B': {'a.cpp': 1}, 'SIMUT_C': {'a.cpp': 1}}),
    ('if constexpr counts', {'a.cpp': 'void f() {\n  if constexpr ( SIMUT_A )\n    g();\n}\n'}, {'SIMUT_A': {'a.cpp': 1}}),
    ('if constexpr without spaces counts', {'a.cpp': 'void f() {\n  if constexpr(SIMUT_A) g();\n}\n'}, {'SIMUT_A': {'a.cpp': 1}}),
    ('} else if constexpr counts', {'a.cpp': 'void f() {\n  if constexpr ( SIMUT_A ) {\n    g();\n  } else if constexpr ( SIMUT_B ) {\n    h();\n  }\n}\n'},
     {'SIMUT_A': {'a.cpp': 1}, 'SIMUT_B': {'a.cpp': 1}}),
    ('else if constexpr on its own line counts', {'a.cpp': 'void f() {\n  if constexpr ( SIMUT_A )\n    g();\n  else if constexpr ( SIMUT_B )\n    h();\n}\n'},
     {'SIMUT_A': {'a.cpp': 1}, 'SIMUT_B': {'a.cpp': 1}}),
    ('several macros in one condition count once each', {'a.cpp': '  if constexpr ( SIMUT_A && SIMUT_B || SIMUT_A )\n    g();\n'}, {'SIMUT_A': {'a.cpp': 1}, 'SIMUT_B': {'a.cpp': 1}}),
    ('an #if and an if constexpr on one macro are two sites', {'a.cpp': '#if SIMUT_A\n#endif\n  if constexpr ( SIMUT_A )\n    g();\n'}, {'SIMUT_A': {'a.cpp': 2}}),
    ('an if constexpr that tests no feature macro counts nothing', {'a.cpp': '  if constexpr ( sizeof( int ) == 4 )\n    g();\n'}, {}),
    ('a comment line counts nothing', {'a.cpp': '// if constexpr ( SIMUT_A )\n/* if constexpr ( SIMUT_A )\n * if constexpr ( SIMUT_A )\n */\n'}, {}),
    ('the files the gate skips count nothing', {'NetCaps.h': '  if constexpr ( SIMUT_A )\n'}, {}),
    ('a file that is not source counts nothing', {'a.txt': '  if constexpr ( SIMUT_A )\n'}, {}),
]

ARV = {'a.cpp': '#if SIMUT_A\n#endif\nvoid f() {\n  if constexpr ( SIMUT_A )\n    g();\n}\n', 'x.cpp': '#if SIMUT_X\n#endif\n'}


def rodar_casos(mod):
    """The messages of the cases `mod` (a copy of the gate) fails."""
    falhas = []
    for nome, arquivos, quer in CASOS:
        got = conta(mod, arquivos)
        if got != quer:
            falhas.append(f'{nome}: the gate counts {got}, expected {quer}')
    rc, out = veredito(mod, ARV, {'SIMUT_A': {'a.cpp': 1}, 'SIMUT_X': {'x.cpp': 1}})
    if rc != 1 or 'a.cpp' not in out or '1 -> 2' not in out:
        falhas.append(f'the verdict: a file one site over its watermark must exit 1 and name it (1 -> 2): exit {rc}: {out.strip()[:160]!r}')
    rc, out = veredito(mod, ARV, {'SIMUT_A': {'a.cpp': 2}, 'SIMUT_X': {'x.cpp': 1}})
    if rc != 0:
        falhas.append(f'the verdict: a file at its watermark must exit 0: exit {rc}: {out.strip()[:160]!r}')
    return falhas


def arvore_real(mod):
    """The real glue's `if constexpr ( SIMUT_NET_WIFI )` is a site for the real gate."""
    rel = 'ui/angulo/DisplayManager_UiA.cpp'
    texto = (ROOT / 'src' / rel).read_text(encoding='utf-8')
    n_if = len(re.findall(r'(?m)^\s*#\s*(?:if|ifdef|ifndef|elif)\b[^\n]*\bSIMUT_NET_WIFI\b', texto))
    n_ce = len(re.findall(r'(?m)^\s*(?:\}\s*)?(?:else\s+)?if\s+constexpr\b[^\n]*\bSIMUT_NET_WIFI\b', texto))
    mod.SRC = str(ROOT / 'src')
    got = mod.scan().get('SIMUT_NET_WIFI', {}).get(rel, 0)
    confere(n_ce >= 1, f'{rel} has no `if constexpr ( SIMUT_NET_WIFI )` line any more: the glyph of the status bar moved, and this case with it')
    confere(got == n_if + n_ce, f'the gate counts {got} sites of SIMUT_NET_WIFI in {rel}; its source has {n_if} preprocessor line(s) and {n_ce} if constexpr line(s)')


# (name, the gate's statement, what replaces it)
MUTANTES = [
    ('the if constexpr rule not used (the gate of 032341e1)', 'm = COND.match(line) or CONSTEXPR.match(line)', 'm = COND.match(line)'),
    ('`else` not accepted before an if constexpr', r'(?:else\s+)?', ''),
    ('`} else` not accepted before an if constexpr', r'(?:\}\s*)?', ''),
    ('the line anchor dropped: a comment line counts', r'^\s*(?:\}\s*)?(?:else\s+)?if\s+constexpr', r'^.*?\bif\s+constexpr'),
    ('every macro of a line counted, not each once', 'for mac in set(MACRO.findall(m.group(1))):', 'for mac in MACRO.findall(m.group(1)):'),
]


def main():
    real = carrega(GATE, 'gate_real')
    falhas = rodar_casos(real)
    CONTA[0] += len(CASOS) + 2
    for f in falhas:
        confere(False, f)
    arvore_real(real)
    if falhas:
        print('mutants not run: the unmutated gate fails a case (the base has no rule to mutate)')
    else:
        fonte = GATE.read_text(encoding='utf-8')
        for nome, antigo, novo in MUTANTES:
            if not confere(fonte.count(antigo) == 1, f'mutant "{nome}": its anchor `{antigo}` is not in check_feature_sprawl.py exactly once'):
                continue
            with tempfile.TemporaryDirectory() as t:
                p = pathlib.Path(t) / 'check_feature_sprawl.py'
                p.write_text(fonte.replace(antigo, novo, 1), encoding='utf-8')
                try:
                    mut = carrega(p, 'gate_mutante')
                except Exception as e:  # noqa: BLE001 (a mutant that does not load is a harness failure, whatever it raised)
                    confere(False, f'mutant "{nome}" does not load: {e}')
                    continue
                f = rodar_casos(mut)
            confere(bool(f), f'mutant "{nome}" must fail a case, and it passed them all')
            if f:
                print(f'  killed: {nome} ({f[0][:120]})')
    print(f'RESULT: {CONTA[0]} checks, {len(FALHAS)} failed')
    return 1 if FALHAS else 0


if __name__ == '__main__':
    sys.exit(main())
