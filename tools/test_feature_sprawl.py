#!/usr/bin/env python3
"""test_feature_sprawl.py -- what tools/check_feature_sprawl.py counts, on the host (S10c, the Wi-Fi status bar fix).

The gate counts, per SIMUT_ macro and per file, the places in src/ that test the macro, and holds them to the watermark in tools/feature_sprawl.json. Until 2026-10-30 a "place" was a preprocessor line (`#if`, `#ifdef`,
`#ifndef`, `#elif`), so a file could branch on a feature with `if constexpr ( SIMUT_X )` and the gate saw nothing: DisplayManager_UiA.cpp's network glyph did exactly that, for a day, to keep the file at one `#if` on
the network class. The gate counts that line as well now, and this holds how (the gate had no test before):

  1. What counts, on temporary trees: the four preprocessor lines (the control), and `if constexpr ( SIMUT_X )` (with and without spaces, after `} else`, after `else`), each macro of a condition once; an `#if` and
     an `if constexpr` on one macro are two sites.
  2. What does not: an `if constexpr` that tests no SIMUT_ macro, a comment line, the text of an end-of-line comment (`// SIMUT_A only`, `/* ... */`: the code is read and not the comment; a `//` inside a string is no comment, and a `'"'` literal opens no string), the files the gate skips, a file that is not source. And what the gate does not see, pinned (the cases marked NOT SEEN): a plain `if ( SIMUT_X )`, a ternary, a `static_assert`, a condition split over lines.
  3. The verdict: a tree with one site more than its watermark exits 1 and names the file; with the watermark raised, 0.
  4. The real tree: the glue's `if constexpr ( SIMUT_NET_WIFI )` is seen by the real gate.
  5. The mutants: the gate's source edited one rule at a time (the `if constexpr` rule not used, `else` not accepted, `} else` not accepted, the line anchor dropped so that a comment line counts, every macro of a
     line counted and not each once, the comment not stripped, a `//` in a string taken for a comment, a block comment not stripped, a character literal not skipped, the digit separator taken for one, an escape in one not skipped); each must fail a case, and an edit whose anchor is not found exactly once fails this script.

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
    # The five selection headers of the UI driver (S11 A5, 2026-10-30): UiDriverTouch.h, the fifth, picks the touch chain with an `#if SIMUT_UI_ANGULO` around an include, as the four before it pick the members, the hooks and the web part.
    ('the five selection headers of the UI driver count nothing', {'ui/UiDriver.h': '#if SIMUT_UI_ANGULO\n#endif\n', 'ui/UiDriverMembers.h': '#if SIMUT_UI_ANGULO\n#endif\n',
      'ui/UiDriverHooks.h': '#if SIMUT_UI_ANGULO\n#endif\n', 'ui/UiDriverWeb.h': '#if SIMUT_UI_ANGULO\n#endif\n', 'ui/UiDriverTouch.h': '#if SIMUT_UI_ANGULO\n#endif\n'}, {}),
    ('a sixth header of that name is not let off: it counts', {'ui/UiDriverFoo.h': '#if SIMUT_UI_ANGULO\n#endif\n'}, {'SIMUT_UI_ANGULO': {'ui/UiDriverFoo.h': 1}}),
    # The code and not the comment: a macro named in an end-of-line comment is no site (2026-10-30, the reviewer's false positive).
    ('an if constexpr whose comment names a macro counts nothing', {'a.cpp': '  if constexpr ( kX )   // SIMUT_A only\n    g();\n'}, {}),
    ('an #if whose comment names a macro counts nothing', {'a.cpp': '#if FOO   // SIMUT_A only\n#endif\n'}, {}),
    ('the macro before a /* */ or // comment counts, the one in the comment does not', {'a.cpp': '#if SIMUT_A   /* not SIMUT_B */\n#endif\n  if constexpr ( SIMUT_C )   // and SIMUT_D\n    g();\n'},
     {'SIMUT_A': {'a.cpp': 1}, 'SIMUT_C': {'a.cpp': 1}}),
    ('a // inside a string is no comment: the macro after it counts', {'a.cpp': '  if constexpr ( sizeof( "//" ) == 3 && SIMUT_A )\n    g();\n'}, {'SIMUT_A': {'a.cpp': 1}}),
    # A character literal is no string and no comment (the final review, 2026-10-30): '"' opened a "string" that swallowed the end of the line, the comment included, and counted the macro in it.
    ('a character literal holding a double quote is no string: the comment after it is still a comment', {'a.cpp': '  if constexpr ( c == \'"\' && SIMUT_A )   // SIMUT_B\n    g();\n'}, {'SIMUT_A': {'a.cpp': 1}}),
    ('a double-quote literal and a string in one condition', {'a.cpp': '  if constexpr ( c == \'"\' && s == "x" && SIMUT_A )   // SIMUT_B\n    g();\n'}, {'SIMUT_A': {'a.cpp': 1}}),
    ('an escaped quote and an escaped backslash in character literals do not end them early', {'a.cpp': "  if constexpr ( c == '\\'' && d == '\\\\' && SIMUT_A )   // SIMUT_B\n    g();\n"}, {'SIMUT_A': {'a.cpp': 1}}),
    ('a digit separator is no character literal: the macro after it counts, the one in the comment does not', {'a.cpp': "  if constexpr ( n == 1'000 && SIMUT_A )   // SIMUT_B\n    g();\n"}, {'SIMUT_A': {'a.cpp': 1}}),
    # What the gate does not see, pinned (the docstring of the gate says so): if a case here starts to count, the gate was taught more and its docstring and message are to say it.
    ('NOT SEEN: a plain if on a macro', {'a.cpp': '  if ( SIMUT_A )\n    g();\n'}, {}),
    ('NOT SEEN: a ternary on a macro', {'a.cpp': '  int x = SIMUT_A ? 1 : 2;\n'}, {}),
    ('NOT SEEN: a static_assert on a macro', {'a.cpp': '  static_assert( SIMUT_A, "x" );\n'}, {}),
    ('NOT SEEN: an if constexpr whose condition is split over lines', {'a.cpp': '  if constexpr (\n      SIMUT_A )\n    g();\n'}, {}),
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
    ('the if constexpr rule not used (the gate of 032341e1)', 'm = COND.match(linha) or CONSTEXPR.match(linha)', 'm = COND.match(linha)'),
    ('`else` not accepted before an if constexpr', r'(?:else\s+)?', ''),
    ('`} else` not accepted before an if constexpr', r'(?:\}\s*)?', ''),
    ('the line anchor dropped: a comment line counts', r'^\s*(?:\}\s*)?(?:else\s+)?if\s+constexpr', r'^.*?\bif\s+constexpr'),
    ('every macro of a line counted, not each once', 'for mac in set(MACRO.findall(m.group(1))):', 'for mac in MACRO.findall(m.group(1)):'),
    ('the comment not stripped before the match', 'linha = tira_comentario(line)', 'linha = line'),
    ('a // inside a string taken for a comment', "if c == '\"':", 'if False:'),
    ('a /* */ comment not stripped', 'elif c == "/" and d == "*":', 'elif False:'),
    ('a character literal not skipped', 'elif c == "\'" and not (i and linha[i - 1].isalnum() and d.isalnum()):', 'elif False:'),
    ('the digit separator taken for a character literal', 'elif c == "\'" and not (i and linha[i - 1].isalnum() and d.isalnum()):', 'elif c == "\'":'),
    ('an escape in a character literal not skipped', r'''while j < n and linha[j] != "'":
                j += 2 if linha[j] == "\\" else 1''', '''while j < n and linha[j] != "'":
                j += 1'''),
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
