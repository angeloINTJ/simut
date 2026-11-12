#!/usr/bin/env python3
"""test_compare_gfx_fonts.py — o compare_gfx_fonts.py diz o que muda entre dois cabeçalhos GFXfont, e só isso.

  (a) src/ui/original/SansBold9pt8b_latin1.h contra ele mesmo: sai 0, o resumo começa com
      `0 glyph(s) differ` e nada é escrito em disco (nem o bytecode do subset_font.py);
  (b) uma cópia em que o xAdvance da primeira entrada da tabela de glifos sobe 1: sai 1, uma só linha
      começa com `U+` (a desse glifo, com os dois valores) e o resumo diz `1 glyph(s) differ`;
  (c) o que não se lê (um texto, um binário, um caminho que não existe), em A e em B: sai 2, com a
      mensagem em stderr, que cita o arquivo, e nada em stdout;
  (d) o formato, em cabeçalhos montados aqui: só xAdvance, yOffset, width e height, nessa ordem e só os
      que diferem (offset e xOffset não contam); o yAdvance, quando difere, numa linha antes do resumo,
      e sai 1 mesmo que só ele difira;
  (e) as faixas: os glifos se alinham pelo código, então faixas deslocadas comparam o que as duas têm;
      sem código em comum, ou com uma tabela que não tem um glifo por código, sai 2, para que
      `0 glyph(s) differ` nunca queira dizer que nada foi comparado;
  (f) a linha de comando errada (nenhum cabeçalho, um só, três): sai 2, com a linha `Uso:` em stderr e nada em stdout. Com zero
      ou um cabeçalho o comparador também sairia 2 sem a verificação (não leu dois), mas calado: o `Uso:` é o que a prova.

Uso: python3 tools/test_compare_gfx_fonts.py
Saída: 0 se todos os controles passam, 1 se algum falha, 2 se não dá para montar o ensaio.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOL = HERE / "compare_gfx_fonts.py"
FONT = HERE.parent / "src" / "ui" / "original" / "SansBold9pt8b_latin1.h"

# Lidos aqui por conta própria, não com o parse_font_header que o comparador usa: o ensaio não pode
# depender do que ele mede.
GLYPHS = re.compile(r"const\s+GFXglyph\s+\w+\[\]\s+PROGMEM\s*=\s*\{(.*?)\};", re.DOTALL)
ENTRY = re.compile(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}")
STRUCT = re.compile(r"const\s+GFXfont\s+\w+\s+PROGMEM\s*=\s*\{.*?"
                    r"0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*(\d+)\s*\}", re.DOTALL)


class Setup(Exception):
    """O ensaio não se monta (o arquivo de teste mudou de forma): sai 2, não reprova o comparador."""


def compare(*paths):
    return subprocess.run(
        [sys.executable, str(TOOL), *map(str, paths)],
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)


def glyph(offset=0, width=6, height=11, xa=7, xo=0, yo=-10):
    """Uma entrada de GFXglyph, na ordem do struct."""
    return (offset, width, height, xa, xo, yo)


def header(first, glyphs, line_h, last=None):
    """Um cabeçalho GFXfont mínimo, na forma que o subset_font.py grava."""
    last = first + len(glyphs) - 1 if last is None else last
    rows = ",\n".join("    {%d, %d, %d, %d, %d, %d}" % g for g in glyphs)
    return (f"static const uint8_t TesteBitmaps[] PROGMEM = {{\n    0x00\n}};\n\n"
            f"static const GFXglyph TesteGlyphs[] PROGMEM = {{\n{rows}\n}};\n\n"
            f"static const GFXfont Teste PROGMEM = {{\n"
            f"    (uint8_t *)TesteBitmaps, (GFXglyph *)TesteGlyphs,\n"
            f"    0x{first:02X}, 0x{last:02X}, {line_h}}};\n")


class Case:
    """Escreve A e B em arquivos e confere o que o comparador diz."""

    def __init__(self, tmp):
        self.tmp, self.n, self.problems = tmp, 0, []

    def files(self, a_text, b_text):
        self.n += 1
        a, b = self.tmp / f"case{self.n}_a.h", self.tmp / f"case{self.n}_b.h"
        a.write_text(a_text, encoding="utf-8")
        b.write_text(b_text, encoding="utf-8")
        return a, b

    def expect(self, what, a_text, b_text, code, lines):
        """Sai `code` e o stdout é exatamente `lines`."""
        p = compare(*self.files(a_text, b_text))
        if p.returncode != code:
            self.problems.append(f"{what}: saiu {p.returncode}, esperava {code}; stderr {p.stderr.strip()!r}")
        if p.stdout.splitlines() != lines:
            self.problems.append(f"{what}: stdout {p.stdout.splitlines()!r}, esperava {lines!r}")

    def refuse(self, what, a_text, b_text):
        """Sai 2, com uma mensagem em stderr e nada em stdout."""
        p = compare(*self.files(a_text, b_text))
        if p.returncode != 2 or not p.stderr.strip() or p.stdout:
            self.problems.append(f"{what}: saiu {p.returncode} (esperava 2), stderr {p.stderr.strip()!r}, "
                                 f"stdout {p.stdout!r}")


def control_a(tmp):
    problems = []
    p = compare(FONT, FONT)
    lines = p.stdout.splitlines()
    if p.returncode != 0:
        problems.append(f"saiu {p.returncode}, esperava 0")
    if not lines or not lines[-1].startswith("0 glyph(s) differ"):
        problems.append(f"o resumo não começa com '0 glyph(s) differ': {lines[-1:]!r}")
    if any(line.startswith("U+") for line in lines):
        problems.append("há linhas U+ num arquivo comparado com ele mesmo")

    # Não escreve arquivos: uma cópia do comparador e do que ele importa, numa pasta nova, tem depois
    # de uma rodada os mesmos dois arquivos (um __pycache__ seria o bytecode do subset_font.py).
    copy = tmp / "copia"
    copy.mkdir()
    for name in ("compare_gfx_fonts.py", "subset_font.py"):
        shutil.copy(HERE / name, copy / name)
    before = sorted(os.listdir(copy))
    q = subprocess.run([sys.executable, str(copy / "compare_gfx_fonts.py"), str(FONT), str(FONT)],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
    created = sorted(set(os.listdir(copy)) - set(before))
    if created:
        problems.append(f"escreveu em disco: {created}")
    if (q.returncode, q.stdout) != (p.returncode, p.stdout):  # a cópia fez o mesmo trabalho
        problems.append(f"a cópia noutra pasta saiu {q.returncode} e disse {q.stdout!r}, não o mesmo")
    return problems


def control_b(tmp):
    text = FONT.read_text(encoding="utf-8")
    table, struct = GLYPHS.search(text), STRUCT.search(text)
    entry = ENTRY.search(table.group(1)) if table else None
    if not (table and struct and entry):
        raise Setup(f"{FONT.name}: não achei a tabela de glifos, a primeira entrada ou a struct GFXfont")
    offset, width, height, xadv, xoff, yoff = (int(v) for v in entry.groups())
    first, line_h = int(struct.group(1), 16), int(struct.group(3))
    start, end = table.start(1) + entry.start(), table.start(1) + entry.end()
    changed = (text[:start] + "{%d, %d, %d, %d, %d, %d}" % (offset, width, height, xadv + 1, xoff, yoff)
               + text[end:])
    differing = sum(a != b for a, b in zip(text.splitlines(), changed.splitlines()))
    if differing != 1 or len(text.splitlines()) != len(changed.splitlines()):
        raise Setup(f"a cópia devia diferir do original em uma linha, difere em {differing}")
    copy = tmp / "b.h"
    copy.write_text(changed, encoding="utf-8")

    p = compare(FONT, copy)
    lines = p.stdout.splitlines()
    u_lines = [line for line in lines if line.startswith("U+")]
    problems = []
    if p.returncode != 1:
        problems.append(f"saiu {p.returncode}, esperava 1")
    if len(u_lines) != 1:
        problems.append(f"{len(u_lines)} linhas começam com U+, esperava exatamente 1: {u_lines!r}")
    want = f"U+{first:04X} {ascii(chr(first))}: xAdvance {xadv} -> {xadv + 1}"
    if u_lines != [want]:
        problems.append(f"linha U+ {u_lines!r}, esperava {[want]!r}")
    want = f"1 glyph(s) differ, yAdvance {line_h} -> {line_h}"
    if lines[-1:] != [want]:
        problems.append(f"resumo {lines[-1:]!r}, esperava {[want]!r}")
    return problems


def control_c(tmp):
    unreadable = [
        ("texto", "isto não é um cabeçalho GFXfont\n".encode()),
        ("binário", bytes(range(256))),  # tem bytes que não são UTF-8
        ("inexistente", None),
    ]
    problems = []
    for what, content in unreadable:
        bad = tmp / f"ilegivel_{what}.h"
        if content is not None:
            bad.write_bytes(content)
        for side, args in (("A", (bad, FONT)), ("B", (FONT, bad))):
            p = compare(*args)
            # O interpretador também sai 2 quando não acha o script: a mensagem tem de ser a do
            # comparador, que cita o arquivo que não leu.
            if p.returncode != 2 or str(bad) not in p.stderr or p.stdout:
                problems.append(f"{what} como {side}: saiu {p.returncode} (esperava 2), "
                                f"stderr {p.stderr.strip()[-120:]!r}, stdout {p.stdout!r}")
    return problems


def control_d(tmp):
    case = Case(tmp)
    a = header(0x41, [glyph(), glyph(), glyph()], 22)
    # 'A': xAdvance e height; 'B': os quatro campos; 'C': só offset e xOffset, que não contam.
    b = header(0x41, [glyph(xa=8, height=12),
                      glyph(xa=9, yo=-9, width=7, height=12),
                      glyph(offset=5, xo=1)], 23)
    case.expect("campos e yAdvance", a, b, 1, [
        "U+0041 'A': xAdvance 7 -> 8, height 11 -> 12",
        "U+0042 'B': xAdvance 7 -> 9, yOffset -10 -> -9, width 6 -> 7, height 11 -> 12",
        "yAdvance 22 -> 23",
        "2 glyph(s) differ, yAdvance 22 -> 23"])
    case.expect("só o yAdvance difere", a, header(0x41, [glyph(), glyph(), glyph()], 23), 1, [
        "yAdvance 22 -> 23",
        "0 glyph(s) differ, yAdvance 22 -> 23"])
    case.expect("iguais", a, a, 0, ["0 glyph(s) differ, yAdvance 22 -> 22"])
    return case.problems


def control_e(tmp):
    case = Case(tmp)
    # 'A'..'C' contra 'B'..'D': só 'B' e 'C' estão nas duas; alinhadas por posição, 'A' seria
    # comparado com 'B'.
    a = header(0x41, [glyph(width=5), glyph(width=6), glyph(width=7)], 22)
    b = header(0x42, [glyph(width=6), glyph(width=8), glyph(width=9)], 22)
    case.expect("faixas deslocadas, A contra B", a, b, 1, [
        "U+0043 'C': width 7 -> 8",
        "1 glyph(s) differ, yAdvance 22 -> 22"])
    case.expect("faixas deslocadas, B contra A", b, a, 1, [
        "U+0043 'C': width 8 -> 7",
        "1 glyph(s) differ, yAdvance 22 -> 22"])
    two = [glyph(), glyph()]
    case.refuse("sem código em comum", header(0x41, two, 22), header(0x61, two, 22))
    case.refuse("tabela curta para a faixa, como A", header(0x41, two, 22, last=0x43), header(0x41, two, 22))
    case.refuse("tabela curta para a faixa, como B", header(0x41, two, 22), header(0x41, two, 22, last=0x43))
    return case.problems


def control_f(tmp):
    problems = []
    for what, args in (("nenhum cabeçalho", ()), ("um só cabeçalho", (FONT,)), ("três cabeçalhos", (FONT, FONT, FONT))):
        p = compare(*args)
        if p.returncode != 2 or "Uso:" not in p.stderr or p.stdout:
            problems.append(f"{what}: saiu {p.returncode} (esperava 2), stderr {p.stderr.strip()[-120:]!r}, stdout {p.stdout!r}")
    return problems


CONTROLS = [
    ("(a) o cabeçalho contra ele mesmo: sai 0, resumo `0 glyph(s) differ`, nada escrito em disco", control_a),
    ("(b) um xAdvance a mais no primeiro glifo: sai 1 e uma só linha U+, a desse glifo", control_b),
    ("(c) o que não se lê (texto, binário, inexistente): sai 2, com a mensagem em stderr", control_c),
    ("(d) o formato das linhas, do yAdvance e do resumo", control_d),
    ("(e) as faixas: alinhadas pelo código; sem código em comum ou sem um glifo por código, sai 2", control_e),
    ("(f) a linha de comando errada (zero, um ou três cabeçalhos): sai 2 e mostra o Uso:", control_f),
]


def main():
    for needed in (TOOL, FONT):
        if not needed.is_file():
            print(f"test_compare_gfx_fonts: falta {needed}", file=sys.stderr)
            return 2
    passed = True
    try:
        with tempfile.TemporaryDirectory() as d:
            for name, control in CONTROLS:
                problems = control(Path(d))
                print(("[ok] " if not problems else "[FAIL] ") + name)
                for problem in problems:
                    print("       " + problem)
                passed = passed and not problems
    except Setup as e:
        print(f"test_compare_gfx_fonts: o ensaio não se monta: {e}", file=sys.stderr)
        return 2
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
