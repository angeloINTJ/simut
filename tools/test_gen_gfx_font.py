#!/usr/bin/env python3
"""test_gen_gfx_font.py — as opções --require e --y-advance do gen_gfx_font.py, e a saída que elas não podem mudar.

Sem --require, o gerador desenha o .notdef no lugar de um glifo que o TTF não tem
(face.load_char não avisa). --require CHARS recusa o TTF a que falta algum dos caracteres.
--y-advance N grava N no yAdvance do GFXfont, no lugar da altura de linha que o FreeType mede, e só nele.
Cinco controles:

  (a) --require com caracteres que o TTF tem, no fim, no começo e no meio da linha de comando:
      sai 0, grava o arquivo (mais de 1000 bytes) e os três arquivos saem iguais;
  (b) --require com caracteres que faltam: sai 1, escreve em stderr `missing glyphs: U+XXXX ...`
      (todos, na ordem de CHARS, sem repetir), não cria o arquivo e não toca em um que já existia;
  (c) sem --require nem --y-advance, a saída (o arquivo e o stdout) é a da versão sem as opções, a do
      commit f0eba34c (`git show`), com os mesmos argumentos e o mesmo interpretador;
  (d) linha de comando errada (--require sem a lista, --require=CHARS, --require duas vezes): sai 2
      e não grava o arquivo, para que a conferência não seja pulada em silêncio;
  (e) --y-advance N, no fim, no começo e no meio da linha e junto de --require: o arquivo sai igual ao
      que se grava sem a opção, menos o último número da struct GFXfont, que vira N (também para 1, 255 e
      o valor que o FreeType mede), e o stdout diz `yAdvance N`; sem valor, repetido, em `=`, não
      decimal ASCII ou fora de 1..255 sai 2 e não grava o arquivo; com um glifo que falta sai 1.

Uso: .venv-tools/bin/python tools/test_gen_gfx_font.py --ttf CAMINHO.ttf
     O interpretador precisa de freetype-py. O TTF precisa ter ABCabc0123456789° e não ter U+4E00
     nem U+4E8C (o LiberationSans-Bold.ttf serve).
Saída: 0 se todos os controles passam, 1 se algum falha, 2 se não dá para rodar.
"""
import argparse
import hashlib
import re
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


def control_e(ttf, tmp):
    problems = []
    out = tmp / "e.h"
    # A referência: sem a opção, com o yAdvance que o gerador mede. O mesmo <out> em todas as rodadas,
    # porque o stdout cita o caminho.
    ref = run(GEN, ttf, *FIXED, out)
    if ref.returncode != 0 or not out.is_file():
        return [f"sem a opção: saiu {ref.returncode}, arquivo criado: {out.is_file()}; "
                f"stderr {last_line(ref.stderr)!r}"], ""
    ref_text, ref_stdout = out.read_text(), ref.stdout
    measured = re.search(r"yAdvance (\d+)\s*$", ref_stdout)
    struct = re.compile(rf"(  {re.escape(FIXED[1])}, {re.escape(FIXED[2])}, )\d+( }};)")
    if not measured or len(struct.findall(ref_text)) != 1:
        return ["a saída sem a opção não tem a forma que o controle supõe (yAdvance no stdout, uma struct)"], ""
    measured = int(measured.group(1))

    def expected(n):
        text = struct.sub(rf"\g<1>{n}\g<2>", ref_text)
        stdout = re.sub(r"yAdvance \d+(\s*)$", rf"yAdvance {n}\1", ref_stdout)
        return text, stdout

    def rodada(args):
        out.unlink(missing_ok=True)
        return run(GEN, *args), (out.read_text() if out.is_file() else None)

    # O valor passado vira o yAdvance, e só ele muda: um a mais que o medido, os dois extremos do
    # uint8_t e o próprio medido (que tem de sair igual à referência).
    for n in (measured + 1, 1, 255, measured):
        p, text = rodada([ttf, *FIXED, out, "--y-advance", n])
        want_text, want_stdout = expected(n)
        if p.returncode != 0 or text is None:
            problems.append(f"--y-advance {n}: saiu {p.returncode}, arquivo criado: {text is not None}; "
                            f"stderr {last_line(p.stderr)!r}")
            continue
        if text != want_text:
            problems.append(f"--y-advance {n}: o arquivo não é o da referência com só o yAdvance trocado")
        if p.stdout != want_stdout:
            problems.append(f"--y-advance {n}: o stdout é {p.stdout!r}, esperava {want_stdout!r}")

    # Em qualquer posição, e junto de --require: o mesmo arquivo.
    n = measured + 1
    want_text, _ = expected(n)
    layouts = {
        "no fim": [ttf, *FIXED, out, "--y-advance", n],
        "no começo": ["--y-advance", n, ttf, *FIXED, out],
        "no meio": [ttf, *FIXED[:2], "--y-advance", n, *FIXED[2:], out],
        "com --require depois": [ttf, *FIXED, out, "--require", PRESENT, "--y-advance", n],
        "com --require antes": ["--require", PRESENT, "--y-advance", n, ttf, *FIXED, out],
    }
    for where, args in layouts.items():
        p, text = rodada(args)
        if p.returncode != 0 or text != want_text:
            problems.append(f"{where}: saiu {p.returncode}; "
                            f"{'o arquivo difere do esperado' if text is not None else 'não criou o arquivo'}")

    # A linha de comando errada sai 2 e não grava (nem a opção boa salva um N ruim).
    bad = {
        "sem valor": [ttf, *FIXED, out, "--y-advance"],
        "--y-advance=N": [ttf, *FIXED, out, "--y-advance=22"],
        "duas vezes": ["--y-advance", "22", ttf, *FIXED, out, "--y-advance", "23"],
        "não numérico": [ttf, *FIXED, out, "--y-advance", "abc"],
        "vazio": [ttf, *FIXED, out, "--y-advance", ""],
        "zero": [ttf, *FIXED, out, "--y-advance", "0"],
        "256, que não cabe no uint8_t": [ttf, *FIXED, out, "--y-advance", "256"],
        "negativo": [ttf, *FIXED, out, "--y-advance", "-1"],
        "com ponto": [ttf, *FIXED, out, "--y-advance", "22.5"],
        "com sinal": [ttf, *FIXED, out, "--y-advance", "+22"],
        "dígitos não ASCII": [ttf, *FIXED, out, "--y-advance", "٢٢"],
        "o valor é a outra opção": [ttf, *FIXED, out, "--y-advance", "--require", PRESENT],
    }
    for what, args in bad.items():
        p, text = rodada(args)
        if p.returncode != 2:
            problems.append(f"{what}: saiu {p.returncode}, esperava 2")
        if "Uso:" not in p.stderr:
            problems.append(f"{what}: stderr sem a linha Uso: {last_line(p.stderr)!r}")
        if text is not None:
            problems.append(f"{what}: o arquivo foi criado")

    # --require que falha continua mandando: sai 1 e não grava, com ou sem --y-advance.
    p, text = rodada([ttf, *FIXED, out, "--y-advance", measured + 1, "--require", MISSING])
    if p.returncode != 1 or "U+4E00" not in p.stderr or text is not None:
        problems.append(f"--y-advance com um glifo que falta: saiu {p.returncode}, "
                        f"stderr {last_line(p.stderr)!r}, arquivo criado: {text is not None}")
    return problems, f"o FreeType mede yAdvance {measured} neste TTF a {FIXED[0]} pt"


CONTROLS = [
    ("(a) --require com glifos que o TTF tem: sai 0 e grava o arquivo, em qualquer posição", control_a),
    ("(b) --require com glifo que falta: sai 1, lista todos em stderr e não grava o arquivo", control_b),
    (f"(c) sem --require nem --y-advance, a saída é a da versão sem as opções ({BASE})", control_c),
    ("(d) linha de comando errada: sai 2 e não grava o arquivo", control_d),
    ("(e) --y-advance N grava só o yAdvance, em qualquer posição; N ruim sai 2 e não grava", control_e),
]


def main():
    ap = argparse.ArgumentParser(description="Controles do gen_gfx_font.py --require e --y-advance (veja a docstring).")
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
