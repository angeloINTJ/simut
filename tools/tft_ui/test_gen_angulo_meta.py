#!/usr/bin/env python3
"""test_gen_angulo_meta.py -- proves tools/tft_ui/gen_angulo_meta.py, the generator of src/ui/angulo/AnguloMeta.h, and its --check.

AnguloMeta.h holds three tables: kExtra (the code point of every GFX code from 0x7F), kCapX (cap and x height of the nine faces of
ua::Fonte) and the enum IconeNome (the icon names, sorted, then NENHUM). Until 2026-11-11 it came from tools/tft_ui/gerar_meta.js,
which runs only with the TFT plan's delivery and a renderer environment, neither in the git tree. The generator reads the tree:
tools/fonts/angulo_extra.json, the face headers of src/ui/angulo/kit/fontes/ and the registry of src/ui/angulo/AnguloIcones.h.

THE UNIT OF A FINDING, which every mutation control below counts, is one entry of kExtra (by its GFX code), one line of kCapX (by its
face) or one name of the enum IconeNome. `--check` prints one indented line for each unit that differs, and a count above them. Text
around the tables (line 1, the opening comment, the braces) is one finding of its own, `text line N`, only when no unit differs: the
counts in the opening comment follow from the same inputs as the names, so a registry with one name fewer is one finding, the name,
and not two. A mutation that changes one unit must therefore give exactly one finding; two units, exactly two.

Controls (each with its negative, and none touches a tracked file: every write goes to a temporary folder):

  (a) the tree as it is: the text the generator writes is the committed AnguloMeta.h byte for byte but for line 1, which names the new
      generator and no longer gerar_meta.js; two runs write the same bytes; no input is changed;
  (b) once regenerated the whole file is the committed one and --check exits 0 (on the tree's own file, and on the copy the control
      wrote, which isolates every mutation below);
  (c) one mutation at a time, in a temporary copy, and --check exits 1 with exactly the findings expected: the height of the 'H'
      (0x48) of SANS9, of the 'x' (0x78) of MONO12 and of the 'H' of DISP12 in a copy of the face; an entry of a copy of
      angulo_extra.json; a name fewer in a copy of AnguloIcones.h; and in a copy of AnguloMeta.h a kCapX value, a kExtra code point, a
      name fewer, a name more, two names in the other order (`text`), a word of the opening comment (`text`) and line 1 as gerar_meta.js
      wrote it (`text`); and a value of the table of the digit-only faces in a copy of the generator. Two units changed at once give
      exactly two findings;
  (d) what cannot be read is refused with exit 2, a reason in stderr, nothing in stdout and no file written: an unknown option, a
      positional, an option without a value, repeated, or as --option=value; an input that is not there; a face header without 'H' and
      'x' that the table does not cover; an EXTRA table with a gap, with a character that disagrees with its code point, or that is not
      JSON; a registry with a row that is malformed, or none; a folder for --out that does not exist; --out that is a folder. An output
      file that is not there is a finding (exit 1), not a refusal: it was looked for;
  (e) where each of the nine kCapX rows comes from: six faces are measured from their header (SANS9, SANSB9, MONO9, MONO12, DISP9 and
      DISP12) and three from the table of the digit-only faces (DISP18, DISP24 and DISP32, whose codes are 43..57); the three table
      rows equal line 3 of their headers, which the delivery's own generator wrote, and the comparison finds a header that was changed;
      a header that carries 'H' and 'x' is measured even when the table has a row for its face;
  (f) the generator's faces are the order of ua::Fonte in Angulo.h (the nine, then MARCA, which kCapX does not carry);
  (g) at the end the tree's inputs and AnguloMeta.h have the sha256 they had at the start.

--mutants runs the controls against copies of the generator with one change each (MUTANTES) and requires that exactly the expected
controls fail: a control that no change drops does not bite. --gen PATH tests another copy of the generator.

Usage: python3 tools/tft_ui/test_gen_angulo_meta.py [--gen PATH] [--mutants]
Exit: 0 if every control passes, 1 if one fails, 2 if the test cannot be set up (an input of the tree is not readable). Standard
library only: it runs in the `gates` job.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True   # no tools/tft_ui/__pycache__, and none in tools/

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
ROOT = TOOLS.parent
GEN = HERE / 'gen_angulo_meta.py'
SUBSET = TOOLS / 'subset_font.py'
UA = ROOT / 'src' / 'ui' / 'angulo'
FONTES = UA / 'kit' / 'fontes'
META = UA / 'AnguloMeta.h'
ICONES = UA / 'AnguloIcones.h'
EXTRA = TOOLS / 'fonts' / 'angulo_extra.json'
ANGULO_H = UA / 'Angulo.h'

# Line 1 of the committed header until 2026-11-11, which the generator replaced with its own.
LINHA1_ANTIGA = "// GENERATED by tools/tft_ui/gerar_meta.js from the TFT plan's delivery. Do not edit."
DE_TEXTO = ('SANS9', 'SANSB9', 'MONO9', 'MONO12', 'DISP9', 'DISP12')   # measured from their own header
SO_DIGITOS = ('DISP18', 'DISP24', 'DISP32')                            # codes 43..57: the table of the generator
ACHADO = re.compile(r'^  (kExtra 0x[0-9A-F]+|kCapX \w+|IconeNome \w+|text line \d+|file): (.*)$')


class Setup(Exception):
    """The test cannot be set up (an input of the tree is not readable): exit 2, it does not reprove the generator."""


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def run(*args, gen=GEN):
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    return subprocess.run([sys.executable, str(gen), *map(str, args)], capture_output=True, text=True, encoding='utf-8',
                          errors='replace', timeout=120, env=env)


def last_line(text):
    lines = text.strip().splitlines()
    return lines[-1] if lines else ''


def shorter(text, n=3, width=240):
    """The first lines of an output, cut to a width, for a message."""
    return repr([line[:width] for line in text.strip().splitlines()[:n]])


def findings(stdout):
    """[(label, detail)]: the indented finding lines of a --check that exited 1."""
    return [m.groups() for m in (ACHADO.match(line) for line in stdout.splitlines()) if m]


class Ctx:
    """What the controls share: the tree's inputs, as bytes, and the generator under test."""

    def __init__(self, gen):
        self.gen = Path(gen)
        try:
            self.meta = META.read_bytes()
            self.faces = {p.name: p.read_bytes() for p in sorted(FONTES.glob('*.h'))}
            self.extra = EXTRA.read_bytes()
            self.icones = ICONES.read_bytes()
            self.angulo_h = ANGULO_H.read_text(encoding='utf-8')
        except OSError as e:
            raise Setup(f'cannot read {e.filename}: {e.strerror or e}')
        if len(self.faces) < 9:
            raise Setup(f'{FONTES} holds {len(self.faces)} headers, the nine faces of ua::Fonte are expected')

    def snapshot(self):
        """The sha256 of what the test must never change: the tree's inputs and the committed header."""
        files = [META, ICONES, EXTRA, ANGULO_H, *sorted(FONTES.glob('*.h'))]
        return {str(p.relative_to(ROOT)): sha256(p.read_bytes()) for p in files}

    def work(self, folder):
        """A new folder with copies of every input (and, once written, the generator's output over them)."""
        folder = Path(folder)
        (folder / 'fontes').mkdir(parents=True)
        for name, data in self.faces.items():
            (folder / 'fontes' / name).write_bytes(data)
        (folder / 'extra.json').write_bytes(self.extra)
        (folder / 'icones.h').write_bytes(self.icones)
        return Work(self, folder)


class Work:
    """One temporary set of inputs and the generator's output over them."""

    def __init__(self, ctx, folder):
        self.ctx, self.folder, self.gen = ctx, Path(folder), ctx.gen
        self.fontes, self.extra, self.icones = self.folder / 'fontes', self.folder / 'extra.json', self.folder / 'icones.h'
        self.out = self.folder / 'AnguloMeta.h'

    def args(self, *flags, **override):
        """The command line over these copies. An option given as None is left out; the flags (--check) go last."""
        given = {'fontes': self.fontes, 'extra': self.extra, 'icones': self.icones, 'out': self.out, **override}
        return [a for k, v in given.items() if v is not None for a in (f'--{k}', v)] + list(flags)

    def write(self, **override):
        return run(*self.args(**override), gen=self.gen)

    def check(self, **override):
        return run(*self.args('--check', **override), gen=self.gen)

    def edit(self, path, change):
        """Rewrites a copy through change(text) -> text, which must change it."""
        before = Path(path).read_text(encoding='utf-8')
        after = change(before)
        if after == before:
            raise Setup(f'a mutation left {path} unchanged')
        Path(path).write_text(after, encoding='utf-8')


def once(text, old, new):
    if text.count(old) != 1:
        raise Setup(f'`{old}` occurs {text.count(old)} times, one is expected')
    return text.replace(old, new)


def bump_glyph_height(text, code):
    """The text of a GFX header with the height of the glyph of this code one more."""
    pattern = re.compile(r"^(  \{ \d+, \d+, )(\d+)(, \d+, -?\d+, -?\d+ \},  // 0x%02X ')" % code, re.M)
    if len(pattern.findall(text)) != 1:
        raise Setup(f'glyph 0x{code:02X} is not in the table exactly once')
    return pattern.sub(lambda m: m.group(1) + str(int(m.group(2)) + 1) + m.group(3), text)


# ---------------------------------------------------------------- the mutations of (c)
def m_height(face, code):
    return lambda w: w.edit(w.fontes / f'{face}.h', lambda t: bump_glyph_height(t, code))


def m_extra_entry(w):
    """Entry 0 (code 127) becomes another character and its unicode field follows: a valid table with one entry changed."""
    w.edit(w.extra, lambda t: once(once(t, '"char": "á"', '"char": "ä"'), '"U+00E1"', '"U+00E4"'))


def m_icones_row(name, size):
    """The registry without the row (name, size), which must be there once: the enum loses the name only if that was its only row,
    and the opening comment's count of rows follows either way."""
    def change(t):
        rows = [ln for ln in t.split('\n') if ln.startswith(f'  {{ IconeNome::{name}, {size}, ')]
        if len(rows) != 1:
            raise Setup(f'{name} at {size} px has {len(rows)} rows in the registry, one is expected')
        return t.replace(rows[0] + '\n', '')
    return lambda w: w.edit(w.icones, change)


def m_out(pattern, new):
    """A substitution of exactly one match of `pattern` (multi-line mode) in the copy of AnguloMeta.h."""
    def change(t):
        found = len(re.findall(pattern, t, re.M))
        if found != 1:
            raise Setup(f'{pattern!r} matches {found} times in the output, one is expected')
        return re.sub(pattern, new, t, flags=re.M)
    return lambda w: w.edit(w.out, change)


def m_capx_value(face):
    return m_out(r'^  \{ (\d+), (\d+) \},  // %s$' % face,
                 lambda m: '  { %d, %s },  // %s' % (int(m.group(1)) + 1, m.group(2), face))


def m_extra_code_point(code):
    return m_out(r'^  0x([0-9a-f]{4}),  // 0x%X ' % code, lambda m: '  0x%04x,  // 0x%X ' % (int(m.group(1), 16) + 1, code))


def m_swap_names(a, b):
    return lambda w: w.edit(w.out, lambda t: once(t, f'\n  {a},\n  {b},\n', f'\n  {b},\n  {a},\n'))


def m_line1_old(w):
    w.edit(w.out, lambda t: LINHA1_ANTIGA + t[t.index('\n'):])


# (the mutation, what changes, the findings --check must print: the label of each, in the order the generator prints them; a label
# that ends before its number, as `text line`, is matched as a prefix)
CASOS = [
    ("the height of the 'H' (0x48) of SANS9, in a copy of the face", m_height('SANS9', 0x48), ['kCapX SANS9']),
    ("the height of the 'x' (0x78) of MONO12, in a copy of the face", m_height('MONO12', 0x78), ['kCapX MONO12']),
    ("the height of the 'H' of DISP12 (a face measured, not tabled), in a copy of the face", m_height('DISP12', 0x48), ['kCapX DISP12']),
    ('one entry of a copy of angulo_extra.json (code 127 becomes another character)', m_extra_entry, ['kExtra 0x7F']),
    ('a name fewer in a copy of AnguloIcones.h (UNPLUG, its only row; the opening comment counts one row fewer too)',
     m_icones_row('UNPLUG', 16), ['IconeNome UNPLUG']),
    ("a row fewer in a copy of AnguloIcones.h for a name that keeps another row (LOCK at 16 px, an extra row): the names are the same, "
     "and the count in the opening comment follows", m_icones_row('LOCK', 16), ['text line 4']),
    ("the same for a delivered row (ARROW_BIG_UP at 16 px, `::entrega::`): the count of delivered rows follows",
     m_icones_row('ARROW_BIG_UP', 16), ['text line 4']),
    ('a kCapX value changed in a copy of AnguloMeta.h (SANS9)', m_capx_value('SANS9'), ['kCapX SANS9']),
    ('a kExtra code point changed in a copy of AnguloMeta.h (0x80)', m_extra_code_point(0x80), ['kExtra 0x80']),
    ('a name fewer in a copy of AnguloMeta.h (WRENCH, which the registry has)', m_out(r'^  WRENCH,\n', ''), ['IconeNome WRENCH']),
    ('a name more in a copy of AnguloMeta.h (ZZZ_TESTE, which the registry does not have)',
     m_out(r'^  NENHUM$', '  ZZZ_TESTE,\n  NENHUM'), ['IconeNome ZZZ_TESTE']),
    ('two names in the other order in a copy of AnguloMeta.h (no unit differs, the text does)',
     m_swap_names('ACTIVITY', 'ARROW_BIG_UP'), ['text line']),
    ('a word of the opening comment in a copy of AnguloMeta.h (line 4)',
     m_out(r"^// registry: the (\d+) icons", r'// register: the \1 icons'), ['text line 4']),
    ('line 1 of a copy of AnguloMeta.h as gerar_meta.js wrote it', m_line1_old, ['text line 1']),
]


def one_case(ctx, folder, name, mutate, expected):
    """The problems of one mutation: the copy before it passes --check, and after it --check exits 1 with the findings expected."""
    w = ctx.work(folder)
    p = w.write()
    if p.returncode != 0:
        return [f'{name}: the generator did not write the base copy: exit {p.returncode}, {last_line(p.stderr)!r}']
    problems = []
    q = w.check()
    if q.returncode != 0:
        problems.append(f'{name}: without the mutation --check exited {q.returncode}, expected 0; {last_line(q.stdout)!r}')
    try:
        mutate(w)
    except Setup as e:   # the output of a changed generator may no longer have what the mutation looks for: the control fails
        return problems + [f'{name}: the mutation cannot be applied: {e}']
    files = [w.out, w.icones, w.extra]
    before = {f: f.read_bytes() for f in files}
    p = w.check()
    labels = [label for label, _ in findings(p.stdout)]
    if p.returncode != 1:
        problems.append(f'{name}: --check exited {p.returncode}, expected 1; stdout {last_line(p.stdout)!r}, stderr {last_line(p.stderr)!r}')
    if len(labels) != len(expected) or not all(got.startswith(want) for got, want in zip(labels, expected)):
        problems.append(f'{name}: findings {labels}, expected {expected}')
    if not re.search(rf'\b{len(expected)} findings?\b', p.stdout):
        problems.append(f'{name}: the count does not say {len(expected)}: {p.stdout.strip().splitlines()[:1]}')
    if p.stderr.strip():
        problems.append(f'{name}: stderr is not empty: {last_line(p.stderr)!r}')
    if any(f.read_bytes() != data for f, data in before.items()):
        problems.append(f'{name}: --check changed a file')
    return problems


def control_c(tmp, ctx):
    problems = []
    for i, (name, mutate, expected) in enumerate(CASOS):
        problems += one_case(ctx, tmp / f'c{i}', name, mutate, expected)
    # Two units changed at once give two findings: the count is by unit, not "at most one".
    w = ctx.work(tmp / 'two')
    w.write()
    m_height('SANS9', 0x48)(w)
    m_height('MONO12', 0x78)(w)
    p = w.check()
    labels = [label for label, _ in findings(p.stdout)]
    if p.returncode != 1 or labels != ['kCapX SANS9', 'kCapX MONO12'] or '2 findings' not in p.stdout:
        problems.append(f'two units changed: exit {p.returncode}, findings {labels}, expected the two kCapX rows')
    # A value of the table of the digit-only faces lives in the generator: one of its copies carries the mutation (MUTANTES has the rest).
    anchor = "'DISP18': (23, 19)"
    source = ctx.gen.read_text(encoding='utf-8')
    if source.count(anchor) != 1:
        problems.append(f'the table of the digit-only faces: `{anchor}` is not in the generator exactly once')
    else:
        mini = tmp / 'gen' / 'tools' / 'tft_ui'
        mini.mkdir(parents=True)
        shutil.copy(SUBSET, mini.parent / 'subset_font.py')
        (mini / 'gen_angulo_meta.py').write_text(source.replace(anchor, "'DISP18': (23, 20)"), encoding='utf-8')
        w = ctx.work(tmp / 'tabled')
        w.write()
        q = run(*w.args('--check'), gen=mini / 'gen_angulo_meta.py')
        labels = [label for label, _ in findings(q.stdout)]
        if q.returncode != 1 or labels != ['kCapX DISP18']:
            problems.append(f'a value of the digit-only table changed in a copy of the generator: exit {q.returncode}, findings {labels}, '
                            f'expected exactly kCapX DISP18')
    return problems, f'{len(CASOS)} mutations, two units at once, and the generator\'s table; each against its isolation control'


def control_a(tmp, ctx):
    problems = []
    w = ctx.work(tmp / 'a')
    before = ctx.snapshot()
    p = w.write()
    if p.returncode != 0:
        return [f'the generator exited {p.returncode}, expected 0; stdout {last_line(p.stdout)!r}, stderr {last_line(p.stderr)!r}'], ''
    if p.stderr.strip():
        problems.append(f'stderr is not empty: {last_line(p.stderr)!r}')
    made = w.out.read_bytes()

    def body(data):
        return data.split(b'\n', 1)[1] if b'\n' in data else b''
    if body(made) != body(ctx.meta):
        have, got = ctx.meta.decode('utf-8', 'replace').split('\n'), made.decode('utf-8', 'replace').split('\n')
        at = next((i for i, (a, b) in enumerate(zip(have, got)) if a != b), min(len(have), len(got)))
        problems.append(f'what the generator writes is not the committed AnguloMeta.h but for line 1: the first difference is line {at + 1}, '
                        f'committed {have[at:at + 1]!r}, generated {got[at:at + 1]!r}')
    first = made.split(b'\n', 1)[0].decode('utf-8', 'replace')
    if not first.startswith('// GENERATED by tools/tft_ui/gen_angulo_meta.py') or 'gerar_meta.js' in first:
        problems.append(f'line 1 does not name the generator: {first!r}')
    q = w.write()   # the same inputs over the same output: the bytes do not move
    if q.returncode != 0 or w.out.read_bytes() != made:
        problems.append('a second run did not write the same bytes')
    if ctx.snapshot() != before:
        problems.append('the generator changed a tracked file')
    return problems, f'{len(made)} B, sha256 {sha256(made)[:12]}...'


def control_b(tmp, ctx):
    problems = []
    w = ctx.work(tmp / 'b')
    w.write()
    p = w.check()
    if p.returncode != 0 or 'is what the tree generates' not in p.stdout:
        problems.append(f'--check on the copy the control wrote: exit {p.returncode}, stdout {shorter(p.stdout)}')
    if w.out.read_bytes() != ctx.meta:
        problems.append('the committed AnguloMeta.h is not what the generator writes, byte for byte: run python3 tools/tft_ui/gen_angulo_meta.py')
    # The tree's own inputs and file: named here, so that a copy of the generator in another folder looks at the same tree.
    q = run('--check', '--fontes', FONTES, '--extra', EXTRA, '--icones', ICONES, '--out', META, gen=ctx.gen)
    if q.returncode != 0:
        problems.append(f'--check on the tree: exit {q.returncode}, expected 0; stdout {shorter(q.stdout)}, stderr {last_line(q.stderr)!r}')
    if ctx.gen == GEN:   # and the generator itself knows where the tree is
        r = run('--check', gen=ctx.gen)
        if r.returncode != 0 or r.stdout != q.stdout:
            problems.append(f'--check with no option: exit {r.returncode}, stdout {shorter(r.stdout)}, not the same as with the paths named')
    return problems, last_line(q.stdout)


def control_d(tmp, ctx):
    problems = []
    count = 0

    def refused(what, prepare, expect, **override):
        """One refusal: exit 2 and the reason in stderr, stdout empty, and the sentinel in place of the output left as it was."""
        nonlocal count
        count += 1
        w = ctx.work(tmp / f'd{count}')
        w.out.write_bytes(b'sentinel\n')
        flags = override.pop('flags', [])
        prepare(w)
        p = run(*w.args(*flags, **override), gen=ctx.gen)
        if p.returncode != 2:
            problems.append(f'{what}: exit {p.returncode}, expected 2; stdout {last_line(p.stdout)!r}, stderr {last_line(p.stderr)!r}')
        if not p.stderr.strip() or expect not in p.stderr:
            problems.append(f'{what}: stderr does not say {expect!r}: {last_line(p.stderr)!r}')
        if p.stdout.strip():
            problems.append(f'{what}: stdout is not empty: {last_line(p.stdout)!r}')
        if w.out.is_file() and w.out.read_bytes() != b'sentinel\n':
            problems.append(f'{what}: the output file was touched')

    def nothing(w):
        pass

    # The command line. --out always goes with them, onto the sentinel, so that a generator that does not refuse shows it.
    for what, flags in (('an unknown option', ['--rapido']), ('a positional', ['algo']), ('--check twice', ['--check', '--check']),
                        ('--out twice', ['--out', 'outro.h']), ('--extra=FILE', ['--extra=x.json']), ('--out=FILE', ['--out=x.h'])):
        refused(what, nothing, 'Usage:', flags=flags)
    refused('--fontes without a value', nothing, 'Usage:', fontes=None, icones=None, extra=None, out=None, flags=['--fontes'])
    refused('an input that is not there (--extra)', nothing, 'not there', extra=tmp / 'nao_existe.json')
    refused('an input that is not there (--icones)', nothing, 'not there', icones=tmp / 'nao_existe.h')
    refused('a folder of faces with none', nothing, 'SANS9.h', fontes=tmp)

    def face_without_hx(w):
        shutil.copy(w.fontes / 'DISP18.h', w.fontes / 'SANS9.h')   # codes 43..57: no 'H', no 'x', and SANS9 is not in the table
    refused("a face header without 'H' and 'x' that the table does not cover", face_without_hx, 'SANS9')
    refused('a header that is not a GFXfont', lambda w: (w.fontes / 'MONO9.h').write_text('nada\n'), 'MONO9.h')
    refused('an EXTRA table with a gap',
            lambda w: w.edit(w.extra, lambda t: once(t, '{"code": 129, "char": "â", "unicode": "U+00E2"},\n', '')), 'EXTRA')
    refused('an EXTRA table whose character disagrees with its code point',
            lambda w: w.edit(w.extra, lambda t: once(t, '"U+00E1"', '"U+00E2"')), 'EXTRA')
    refused('an EXTRA table that is not JSON', lambda w: w.extra.write_text('isto nao e JSON'), 'EXTRA')
    refused('a registry row that is malformed',
            lambda w: w.edit(w.icones, lambda t: once(t, '{ IconeNome::UNPLUG, 16, EXTRA_UNPLUG_16_bits },', '{ IconeNome::UNPLUG, 16 },')),
            'registry')
    refused('a registry with no row', lambda w: w.icones.write_text('// vazio\n'), 'registry')
    refused('--out in a folder that does not exist', nothing, 'write', out=tmp / 'nao_existe' / 'x.h')
    refused('--out that is a folder, with --check', nothing, str(tmp), out=tmp, flags=['--check'])
    # An output file that is not there is an observation, not a refusal: --check exits 1 and the finding is the file.
    w = ctx.work(tmp / 'dmissing')
    p = w.check()
    if p.returncode != 1 or [label for label, _ in findings(p.stdout)] != ['file']:
        problems.append(f'--check with the output file not there: exit {p.returncode}, findings {findings(p.stdout)}, expected exit 1 and `file`')
    return problems, f'{count} command lines and inputs refused; the output file that is not there is reported'


def table_vs_line3(folder, rows):
    """The problems of a digit-only row that differs from line 3 of its header (`Caixa alta N px, altura de x M px`)."""
    problems = []
    for face in SO_DIGITOS:
        text = (Path(folder) / f'{face}.h').read_text(encoding='utf-8')
        m = re.search(r'^// Caixa alta (\d+) px, altura de x (\d+) px', text, re.M)
        if not m:
            problems.append(f'{face}.h has no `Caixa alta N px, altura de x M px` comment')
        elif face in rows and (int(m.group(1)), int(m.group(2))) != rows[face][:2]:
            problems.append(f'{face}: the generator gives {rows[face][:2]}, line 3 of {face}.h says {(int(m.group(1)), int(m.group(2)))}')
    return problems


def control_e(tmp, ctx):
    w = ctx.work(tmp / 'e')
    p = w.write()
    if p.returncode != 0:
        return [f'the generator did not write: exit {p.returncode}, {last_line(p.stderr)!r}'], ''
    rows = {m.group(1): (int(m.group(2)), int(m.group(3)), m.group(4))
            for m in re.finditer(r'^  kCapX (\w+) \{ (\d+), (\d+) \}: (.*)$', p.stdout, re.M)}
    problems = []
    if sorted(rows) != sorted(DE_TEXTO + SO_DIGITOS):
        problems.append(f'stdout does not list the nine faces once each: {sorted(rows)}')
    for face in DE_TEXTO:
        if face in rows and not rows[face][2].startswith(f'measured from {face}.h'):
            problems.append(f'{face}: expected `measured from {face}.h`, stdout says {rows[face][2]!r}')
    for face in SO_DIGITOS:
        if face in rows and not rows[face][2].startswith('the table'):
            problems.append(f'{face}: expected `the table ...`, stdout says {rows[face][2]!r}')
    problems += table_vs_line3(FONTES, rows)
    # The negative: the same comparison over headers whose line 3 was changed.
    changed = tmp / 'e_line3'
    changed.mkdir()
    for face in SO_DIGITOS:
        text = (FONTES / f'{face}.h').read_text(encoding='utf-8')
        (changed / f'{face}.h').write_text(text.replace('Caixa alta ', 'Caixa alta 1', 1), encoding='utf-8')
    if len(table_vs_line3(changed, rows)) != len(SO_DIGITOS):
        problems.append('the comparison with line 3 of the headers did not notice three headers whose figures were changed')
    # A header that carries 'H' and 'x' is measured even if the table has a row for its face: DISP9's glyphs under the name DISP18.h.
    w2 = ctx.work(tmp / 'e_wins')
    shutil.copy(w2.fontes / 'DISP9.h', w2.fontes / 'DISP18.h')
    q = w2.write()
    row = re.search(r'^  kCapX DISP18 \{ (\d+), (\d+) \}: (.*)$', q.stdout, re.M)
    if q.returncode != 0 or not row or (int(row.group(1)), int(row.group(2))) != rows.get('DISP9', ())[:2] \
            or not row.group(3).startswith('measured from DISP18.h'):
        problems.append(f"a header with 'H' and 'x' under the name of a tabled face: exit {q.returncode}, row {row and row.groups()}, "
                        f"expected DISP9's {rows.get('DISP9', ())[:2]} and `measured from DISP18.h`")
    return problems, '; '.join(f'{f} {rows[f][0]}/{rows[f][1]}' for f in DE_TEXTO + SO_DIGITOS if f in rows)


def control_f(tmp, ctx):
    m = re.search(r'enum class Fonte : uint8_t \{([^}]*)\}', ctx.angulo_h)
    if not m:
        return ['Angulo.h: `enum class Fonte` not found'], ''
    enum = [s.strip() for s in m.group(1).split(',') if s.strip()]
    faces = list(DE_TEXTO + SO_DIGITOS)
    problems = []
    if enum != faces + ['MARCA']:
        problems.append(f'ua::Fonte in Angulo.h is {enum}; this test expects {faces + ["MARCA"]}')
    w = ctx.work(tmp / 'f')
    w.write()
    capx = re.findall(r'^  \{ \d+, \d+ \},  // (\w+)$', w.out.read_text(encoding='utf-8'), re.M)
    if capx != faces:
        problems.append(f'the rows of kCapX are in the order {capx}, ua::Fonte is {faces}')
    return problems, ', '.join(enum)


CONTROLES = [
    ('(a) the tree as it is: the text written is the committed AnguloMeta.h but for line 1', control_a),
    ('(b) regenerated, the whole file is the committed one and --check exits 0', control_b),
    ('(c) one mutation at a time: --check exits 1 with exactly the findings expected', control_c),
    ('(d) what cannot be read is refused with exit 2, and writes nothing', control_d),
    ('(e) six faces are measured, three come from the table, and the table is line 3 of the headers', control_e),
    ("(f) the generator's faces are the order of ua::Fonte in Angulo.h", control_f),
]


def run_controls(gen):
    """{title: (problems, detail)} for the generator at `gen`."""
    ctx = Ctx(gen)
    start = ctx.snapshot()
    results = {}
    with tempfile.TemporaryDirectory(prefix='gen_angulo_meta_test.') as d:
        for i, (title, control) in enumerate(CONTROLES):
            sub = Path(d) / f'k{i}'
            sub.mkdir()
            try:
                results[title] = control(sub, ctx)
            except Setup:
                raise
            except Exception as e:   # a control that raises is a control that fails
                results[title] = ([f'raised {type(e).__name__}: {str(e)[:200]}'], '')
    changed = [k for k, v in ctx.snapshot().items() if start.get(k) != v]
    results['(g) the test changed no input of the tree and no committed header'] = ([f'changed: {changed}'] if changed else [], '')
    return results


# ---------------------------------------------------------------- the generator's own mutants
# (name, an anchor that occurs once in the generator, its replacement, the controls that must fail: their letters in CONTROLES)
MUTANTES = [
    ("the cap height is read from the 'x' glyph too",
     'for code in (CAIXA_ALTA, ALTURA_X))',
     'for code in (ALTURA_X, ALTURA_X))',
     {'c', 'a', 'b'}),
    ('a kCapX row written as { x, cap }',
     "*[f'  {{ {cap}, {x} }},  // {face}' for face, cap, x, _ in heights],",
     "*[f'  {{ {x}, {cap} }},  // {face}' for face, cap, x, _ in heights],",
     {'a', 'b'}),
    ('the names sorted by their identifiers, not by their hyphenated names',
     "return sorted(names, key=lambda n: n.lower().replace('_', '-')), delivered, extra",
     'return sorted(names), delivered, extra',
     {'a', 'b'}),
    ('an unknown option is skipped',
     '        else:\n            raise Refused(USO)\n    return check, found',
     '        else:\n            pass\n    return check, found',
     {'d'}),
    ('--check exits 0 with findings',
     "            print('Run python3 tools/tft_ui/gen_angulo_meta.py to write it again from the tree.')\n            return 1",
     "            print('Run python3 tools/tft_ui/gen_angulo_meta.py to write it again from the tree.')\n            return 0",
     {'c'}),
    ('the text around the tables is always a finding',
     '    if not out:   # no unit differs: what differs is the text around them, or the order of the names',
     '    if True:   # no unit differs: what differs is the text around them, or the order of the names',
     {'c'}),
    ('kExtra code points in upper case',
     "*[f'  0x{ord(ch):04x},  // 0x{code:X} {ch}' for code, ch in extra],",
     "*[f'  0x{ord(ch):04X},  // 0x{code:X} {ch}' for code, ch in extra],",
     {'c', 'a', 'b'}),
    ('the counts in the opening comment are constants',
     'f"// registry: the {delivered} icons of icones.h plus {extras} the firmware\'s real screens",',
     '"// registry: the 42 icons of icones.h plus 51 the firmware\'s real screens",',
     {'c'}),
    ('a write that fails is announced as written',
     "            raise Refused(f'{shown(out)}: could not write: {e.strerror or e}')",
     '            pass',
     {'d'}),
    ('a value of the table of the digit-only faces',
     "'DISP24': (31, 24)",
     "'DISP24': (31, 25)",
     {'a', 'e', 'b'}),
    ("the enum takes only the extra rows' names",
     '        names.add(m.group(1))\n',
     '        if not m.group(3):\n            names.add(m.group(1))\n',
     {'c', 'a', 'b'}),
    ('delivered and extra rows counted the other way round',
     '        if m.group(3):\n            delivered += 1\n        else:\n            extra += 1',
     '        if m.group(3):\n            extra += 1\n        else:\n            delivered += 1',
     {'a', 'b'}),
    ('NENHUM is not written',
     "        '  NENHUM',\n",
     '',
     {'c', 'a', 'b'}),
    ('nothing is written, and the text says it was',
     "            out.write_bytes(text.encode('utf-8'))",
     '            pass',
     {'a', 'f', 'c', 'b', 'd'}),
    ('an output file that is not there is a refusal',
     "                print(f'  file: {shown(out)} is not there')\n                return 1",
     "                print(f'  file: {shown(out)} is not there')\n                return 2",
     {'d'}),
    ('a gap in the EXTRA table is accepted',
     ' or [code for code, _, _ in table] != list(range(PRIMEIRO_EXTRA, PRIMEIRO_EXTRA + len(table))):',
     ':',
     {'d'}),
    ('the unicode field of the EXTRA table is not checked',
     " and uni == 'U+%04X' % ord(ch)",
     '',
     {'d'}),
    ('a malformed registry row is skipped',
     "            raise Refused(f'{shown(path)}:{number}: not a row of the registry: {line!r}')",
     '            continue',
     {'d'}),
    ('line 1 still names gerar_meta.js',
     'LINHA1 = "// GENERATED by tools/tft_ui/gen_angulo_meta.py from the tree\'s faces, EXTRA table and icon registry. Do not edit."',
     'LINHA1 = "// GENERATED by tools/tft_ui/gerar_meta.js from the TFT plan\'s delivery. Do not edit."',
     {'c', 'a', 'b'}),
    ('the table of the digit-only faces wins over a measured header',
     '        if all(pair):\n',
     '        if all(pair) and face not in SO_DIGITOS:\n',
     {'e'}),
]


def mutants():
    src = GEN.read_text(encoding='utf-8')
    good = True
    for name, anchor, change, expected in MUTANTES:
        if src.count(anchor) != 1:
            print(f'INVALID {name}: the anchor occurs {src.count(anchor)} times in the generator')
            good = False
            continue
        with tempfile.TemporaryDirectory(prefix='gen_angulo_meta_mut.') as d:
            mini = Path(d) / 'tools' / 'tft_ui'
            mini.mkdir(parents=True)
            shutil.copy(SUBSET, mini.parent / 'subset_font.py')
            (mini / 'gen_angulo_meta.py').write_text(src.replace(anchor, change), encoding='utf-8')
            res = run_controls(mini / 'gen_angulo_meta.py')
        fell = {title[1] for title, (problems, _) in res.items() if problems}
        ok = fell == set(expected)
        good = good and ok
        print(f"{'ok  ' if ok else 'FAIL'} {name}: fails {sorted(fell)} (expected {sorted(expected)})")
    print(f"{len(MUTANTES)} mutants, {'all caught by exactly the expected controls' if good else 'SOME NOT CAUGHT AS EXPECTED'}")
    return 0 if good else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--gen', default=str(GEN), help='the generator to test (a mutant, for example)')
    ap.add_argument('--mutants', action='store_true')
    args = ap.parse_args()
    try:
        if args.mutants:
            return mutants()
        results = run_controls(args.gen)
    except Setup as e:
        print(f'test_gen_angulo_meta: the test cannot be set up: {e}', file=sys.stderr)
        return 2
    bad = 0
    for title, (problems, detail) in results.items():
        print(('[ok] ' if not problems else '[FAIL] ') + title)
        if detail:
            print('       ' + detail)
        for problem in problems[:8]:
            print('       ' + problem)
        bad += bool(problems)
    print(f'{bad} of {len(results)} controls fail')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
