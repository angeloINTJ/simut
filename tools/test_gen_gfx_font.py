#!/usr/bin/env python3
"""test_gen_gfx_font.py — a opção --require do gen_gfx_font.py, e a saída que ela não pode mudar.

Sem --require, o gerador desenha o .notdef no lugar de um glifo que o TTF não tem
(face.load_char não avisa). --require CHARS recusa o TTF a que falta algum dos caracteres.
Quatro controles:

  (a) --require com caracteres que o TTF tem, no fim, no começo e no meio da linha de comando:
      sai 0, grava o arquivo (mais de 1000 bytes) e os três arquivos saem iguais;
  (b) --require com caracteres que faltam: sai 1, escreve em stderr `missing glyphs: U+XXXX ...`
      (todos, na ordem de CHARS, sem repetir), não cria o arquivo e não toca em um que já existia;
  (c) sem --require, a saída (o arquivo e o stdout) é a da versão sem a opção, a do commit f0eba34c
      (`git show`), com os mesmos argumentos e o mesmo interpretador;
  (d) linha de comando errada (--require sem a lista, --require=CHARS, --require duas vezes): sai 2
      e não grava o arquivo, para que a conferência não seja pulada em silêncio.

Uso: .venv-tools/bin/python tools/test_gen_gfx_font.py --ttf CAMINHO.ttf
     O interpretador precisa de freetype-py. O TTF precisa ter ABCabc0123456789° e não ter U+4E00
     nem U+4E8C (o LiberationSans-Bold.ttf serve).
Saída: 0 se todos os controles passam, 1 se algum falha, 2 se não dá para rodar.
"""
import argparse
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
GEN = HERE / "gen_gfx_font.py"
# O último commit do gerador sem --require; depois dele o HEAD já tem a opção.
BASE = "f0eba34c"
FIXED = ["9", "0x20", "0x7E", "Teste"]  # <size> <first> <last> <StructName>
PRESENT = "ABCabc0123456789°"      # ABCabc0123456789°
MISSING = "一"                      # 一
MISSING2 = "二"                     # 二


def run(script, *args):
    return subprocess.run(
        [sys.executable, str(script), *map(str, args)],
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)


def last_line(text):
    """A última linha de `text`: num traceback, o erro."""
    lines = text.strip().splitlines()
    return lines[-1] if lines else ""


def control_a(ttf, tmp):
    layouts = {
        "no fim": lambda out: [ttf, *FIXED, out, "--require", PRESENT],
        "no começo": lambda out: ["--require", PRESENT, ttf, *FIXED, out],
        "no meio": lambda out: [ttf, *FIXED[:2], "--require", PRESENT, *FIXED[2:], out],
    }
    problems, outputs = [], {}
    for i, (where, build) in enumerate(layouts.items()):
        out = tmp / f"a{i}.h"
        p = run(GEN, *build(out))
        if p.returncode != 0:
            problems.append(f"{where}: saiu {p.returncode}, esperava 0; stderr {last_line(p.stderr)!r}")
        elif not out.is_file():
            problems.append(f"{where}: saiu 0 e não criou o arquivo")
        elif out.stat().st_size <= 1000:
            problems.append(f"{where}: o arquivo tem {out.stat().st_size} bytes, esperava mais de 1000")
        else:
            outputs[where] = out.read_bytes()
    if len(set(outputs.values())) > 1:
        problems.append("a posição de --require mudou o arquivo gerado")
    return problems, ""


def control_b(ttf, tmp):
    problems = []
    out = tmp / "b1.h"
    p = run(GEN, ttf, *FIXED, out, "--require", "A" + MISSING)
    if p.returncode != 1:
        problems.append(f"saiu {p.returncode}, esperava 1")
    if "U+4E00" not in p.stderr:
        problems.append(f"stderr não cita U+4E00: {last_line(p.stderr)!r}")
    if out.exists():
        problems.append("o arquivo foi criado")
    if p.stdout:
        problems.append(f"stdout devia estar vazio, tem {p.stdout!r}")

    # Todos os que faltam, na ordem de CHARS, cada um uma vez.
    out = tmp / "b2.h"
    p = run(GEN, "--require", MISSING2 + "A" + MISSING + MISSING, ttf, *FIXED, out)
    want = "missing glyphs: U+4E8C U+4E00"
    if p.stderr.strip() != want:
        problems.append(f"stderr {last_line(p.stderr)!r}, esperava {want!r}")
    if p.returncode != 1 or out.exists():
        problems.append(f"com dois faltando: saiu {p.returncode}, arquivo criado: {out.exists()}")

    # Um arquivo que já existia fica como estava (não é truncado antes da conferência).
    out = tmp / "b3.h"
    out.write_bytes(b"ANTIGO\n")
    run(GEN, ttf, *FIXED, out, "--require", MISSING)
    if out.read_bytes() != b"ANTIGO\n":
        problems.append("o arquivo que já existia foi alterado")
    return problems, ""


def control_c(ttf, tmp):
    git = subprocess.run(["git", "show", f"{BASE}:tools/gen_gfx_font.py"],
                         cwd=HERE, capture_output=True)
    if git.returncode != 0:
        why = git.stderr.decode(errors="replace").strip()
        return [f"não leu a versão base (git show {BASE}:tools/gen_gfx_font.py): {why}"], ""
    base = tmp / "gen_gfx_font_base.py"
    base.write_bytes(git.stdout)

    # O mesmo <out> nas duas rodadas: o stdout cita o caminho, e assim sai igual byte a byte.
    out = tmp / "c.h"
    seen, problems = {}, []
    for label, script in (("base", base), ("atual", GEN)):
        out.unlink(missing_ok=True)
        p = run(script, ttf, *FIXED, out)
        if p.returncode != 0 or not out.is_file():
            problems.append(f"{label}: saiu {p.returncode}, arquivo criado: {out.is_file()}; "
                            f"stderr {last_line(p.stderr)!r}")
            continue
        data = out.read_bytes()
        seen[label] = (hashlib.sha256(data).hexdigest(), p.stdout, len(data))
    if len(seen) == 2:
        if seen["base"][0] != seen["atual"][0]:
            problems.append(f"o arquivo mudou: sha256 {seen['base'][0]} (base) -> {seen['atual'][0]}")
        if seen["base"][1] != seen["atual"][1]:
            problems.append(f"o stdout mudou: {seen['base'][1]!r} (base) -> {seen['atual'][1]!r}")
    detail = f"sha256 {seen['atual'][0]} ({seen['atual'][2]} bytes)" if "atual" in seen else ""
    return problems, detail


def control_d(ttf, tmp):
    cases = {
        "--require sem a lista": [ttf, *FIXED, "{out}", "--require"],
        "--require=CHARS": [ttf, *FIXED, "{out}", "--require=" + PRESENT],
        "--require duas vezes": ["--require", "A", ttf, *FIXED, "{out}", "--require", "B"],
    }
    problems = []
    for i, (what, args) in enumerate(cases.items()):
        out = tmp / f"d{i}.h"
        p = run(GEN, *[str(out) if a == "{out}" else a for a in args])
        if p.returncode != 2:
            problems.append(f"{what}: saiu {p.returncode}, esperava 2")
        if "Uso:" not in p.stderr:
            problems.append(f"{what}: stderr sem a linha Uso: {last_line(p.stderr)!r}")
        if out.exists():
            problems.append(f"{what}: o arquivo foi criado")
    return problems, ""


CONTROLS = [
    ("(a) --require com glifos que o TTF tem: sai 0 e grava o arquivo, em qualquer posição", control_a),
    ("(b) --require com glifo que falta: sai 1, lista todos em stderr e não grava o arquivo", control_b),
    (f"(c) sem --require, a saída é a da versão sem a opção ({BASE})", control_c),
    ("(d) linha de comando errada: sai 2 e não grava o arquivo", control_d),
]


def main():
    ap = argparse.ArgumentParser(description="Controles do gen_gfx_font.py --require (veja a docstring).")
    ap.add_argument("--ttf", required=True, help="TTF com ABCabc0123456789° e sem U+4E00 nem U+4E8C")
    ttf = Path(ap.parse_args().ttf).resolve()

    # O ensaio só vale se o TTF é o que os controles supõem; lido direto, sem o gerador.
    try:
        import freetype
    except ImportError:
        print("test_gen_gfx_font: freetype-py não está neste interpretador; "
              "rode com .venv-tools/bin/python", file=sys.stderr)
        return 2
    try:
        face = freetype.Face(str(ttf))
    except Exception as e:  # noqa: BLE001 (o erro do FreeType não tem classe própria estável)
        print(f"test_gen_gfx_font: não abriu {ttf}: {e}", file=sys.stderr)
        return 2
    wrong = [f"U+{ord(c):04X} falta no TTF" for c in PRESENT if not face.get_char_index(ord(c))]
    wrong += [f"U+{ord(c):04X} existe no TTF" for c in MISSING + MISSING2 if face.get_char_index(ord(c))]
    if wrong:
        print(f"test_gen_gfx_font: {ttf} não serve aos controles: " + "; ".join(wrong), file=sys.stderr)
        return 2

    passed = True
    with tempfile.TemporaryDirectory() as d:
        for name, control in CONTROLS:
            problems, detail = control(str(ttf), Path(d))
            print(("[ok] " if not problems else "[FAIL] ") + name)
            if detail:
                print("       " + detail)
            for problem in problems:
                print("       " + problem)
            passed = passed and not problems
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
