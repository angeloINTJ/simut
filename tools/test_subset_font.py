#!/usr/bin/env python3
"""test_subset_font.py — a opção --provenance do subset_font.py, e a saída que ela não pode mudar.

--provenance TEXT grava `// TEXT` como a PRIMEIRA linha do cabeçalho que o subset_font.py gera, para que
de onde vieram os glifos (o TTF, a versão, o sha256 do pino) fique no arquivo e ninguém o edite à mão.
Sem a opção a saída é a de sempre. Cinco controles, sobre um cabeçalho completo montado aqui, na forma
em que o gen_gfx_font.py o grava (nem freetype nem TTF: roda com o python3 do sistema):

  (a) com a opção, no fim, no começo e no meio da linha de comando: o arquivo é `// TEXT` e, depois dela,
      o arquivo que a mesma linha grava sem a opção, byte a byte, e o stdout é o mesmo;
  (b) controle negativo: sem a opção a primeira linha é `/*`, nenhuma linha começa com `//` e o texto
      não está em lugar nenhum do arquivo;
  (c) sem a opção, o arquivo e o stdout são os do script de antes da opção (sha256 e stdout medidos com o
      subset_font.py do commit 51285533, na mesma entrada e com os mesmos caminhos relativos);
  (d) linha de comando errada (--provenance sem valor, repetido, na forma --provenance=TEXT, posicional a
      mais ou a menos) e texto que não cabe numa linha de comentário (vazio, com quebra de linha, retorno
      de carro, tabulação, DEL, barra invertida, fora do ASCII): sai 1, escreve a razão ou a linha Usage:
      em stderr, não cria o arquivo e não toca em um que já existia;
  (e) o leitor: parse_font_header, que o compare_gfx_fonts.py usa, lê do arquivo com a linha o mesmo que
      do arquivo sem ela, e o que o cabeçalho montado aqui diz.

Uso: python3 tools/test_subset_font.py
Saída: 0 se todos os controles passam, 1 se algum falha, 2 se não dá para montar o ensaio.
"""
import hashlib
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOL = HERE / "subset_font.py"

# O que o subset_font.py de 51285533 (sem a opção) grava para o cabeçalho de full_header(), com
# `full.h` e `out.h` como caminhos relativos (o arquivo cita o de origem e o stdout cita o de destino).
GOLDEN_SHA256 = "d49816b65ca87d5e9e2fb6a08e72976f31597e4670eec6dce196e539a66a8f19"
GOLDEN_STDOUT = 'subset_font: 14 -> 8 bytes (saved 6). Wrote out.h\n'

NAME = "Teste"
FIRST, COUNT, LINE_H = 0x41, 5, 22
KEEP = "ACD"
TEXT = ("LiberationSans-Bold.ttf 2.1.5 (sha256 788abee4c806d660e8aee46689dd8540cd4bb98da03dcc9d171ce3efd99a9173), "
        "SIL OFL 1.1: tools/fonts/liberation.json")


def full_header():
    """Um cabeçalho completo, linha a linha como o gen_gfx_font.py o grava: (texto, bytes dos bitmaps, glifos)."""
    bitmaps, glyphs = [], []
    for i in range(COUNT):
        width, height = 3 + i, 4
        data = [(0x11 * (i + 1) + k) & 0xFF for k in range((width * height + 7) // 8)]
        glyphs.append((len(bitmaps), width, height, 5 + i, i % 2, -3 - i))
        bitmaps += data
    lines = [f"const uint8_t {NAME}Bitmaps[] PROGMEM = {{"]
    for i in range(0, len(bitmaps), 12):
        chunk = ", ".join(f"0x{b:02X}" for b in bitmaps[i:i + 12])
        lines.append(f"  {chunk}{',' if i + 12 < len(bitmaps) else ''}")
    lines.append("};\n")
    lines.append(f"const GFXglyph {NAME}Glyphs[] PROGMEM = {{")
    for i, (off, w, h, xa, xo, yo) in enumerate(glyphs):
        lines.append(f"  {{ {off:5d}, {w:3d}, {h:3d}, {xa:3d}, {xo:4d}, {yo:4d} }}"
                     f"{',' if i < COUNT - 1 else ''} // 0x{FIRST + i:02X} '{chr(FIRST + i)}'")
    lines.append("};\n")
    lines.append(f"const GFXfont {NAME} PROGMEM = {{")
    lines.append(f"  (uint8_t  *){NAME}Bitmaps,")
    lines.append(f"  (GFXglyph *){NAME}Glyphs,")
    lines.append(f"  0x{FIRST:02X}, 0x{FIRST + COUNT - 1:02X}, {LINE_H} }};\n")
    lines.append(f"// Approx. {len(bitmaps) + COUNT * 7 + 7} bytes\n")
    return "\n".join(lines), bitmaps, glyphs


def run(cwd, *args):
    """O subset_font.py, de dentro de `cwd` (os caminhos são relativos: o arquivo e o stdout os citam)."""
    return subprocess.run(
        [sys.executable, str(TOOL), *map(str, args)], cwd=cwd,
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)


def last_line(text):
    lines = text.strip().splitlines()
    return lines[-1] if lines else ""


def fresh(tmp):
    """Uma pasta nova com full.h, e o que cada rodada grava em out.h."""
    d = Path(tempfile.mkdtemp(dir=tmp))
    (d / "full.h").write_text(full_header()[0], encoding="utf-8")
    return d


def without_option(tmp):
    """(arquivo, stdout) de uma rodada sem a opção."""
    d = fresh(tmp)
    p = run(d, "full.h", "out.h", KEEP)
    if p.returncode != 0 or not (d / "out.h").is_file():
        raise Setup(f"sem a opção: saiu {p.returncode}; stderr {last_line(p.stderr)!r}")
    return (d / "out.h").read_bytes(), p.stdout


class Setup(Exception):
    """O ensaio não se monta: sai 2, não reprova o subset_font.py."""


def control_a(tmp):
    base_file, base_stdout = without_option(tmp)
    want = ("// " + TEXT + "\n").encode() + base_file
    layouts = {
        "no fim": ["full.h", "out.h", KEEP, "--provenance", TEXT],
        "no começo": ["--provenance", TEXT, "full.h", "out.h", KEEP],
        "no meio": ["full.h", "--provenance", TEXT, "out.h", KEEP],
    }
    problems = []
    for where, args in layouts.items():
        d = fresh(tmp)
        p = run(d, *args)
        got = (d / "out.h").read_bytes() if (d / "out.h").is_file() else None
        if p.returncode != 0 or got is None:
            problems.append(f"{where}: saiu {p.returncode}, arquivo criado: {got is not None}; "
                            f"stderr {last_line(p.stderr)!r}")
            continue
        if got != want:
            first = got.decode("utf-8", "replace").splitlines()[:1]
            problems.append(f"{where}: o arquivo não é `// TEXT` seguido do arquivo sem a opção "
                            f"(primeira linha {first!r})")
        if p.stdout != base_stdout:
            problems.append(f"{where}: o stdout é {p.stdout!r}, esperava {base_stdout!r}")
    return problems, ""


def control_b(tmp):
    data, _ = without_option(tmp)
    text = data.decode("utf-8")
    problems = []
    if text.splitlines()[0] != "/*":
        problems.append(f"sem a opção, a primeira linha é {text.splitlines()[0]!r}, esperava '/*'")
    if any(line.startswith("//") and "Approx." not in line for line in text.splitlines()):
        problems.append("sem a opção, há uma linha de comentário `//` que não é a do tamanho")
    if "sha256" in text or "SIL OFL" in text:
        problems.append("sem a opção, o texto de proveniência está no arquivo")
    return problems, ""


def control_c(tmp):
    data, stdout = without_option(tmp)
    got = hashlib.sha256(data).hexdigest()
    problems = []
    if got != GOLDEN_SHA256:
        problems.append(f"o arquivo mudou: sha256 {GOLDEN_SHA256} (51285533) -> {got}")
    if stdout != GOLDEN_STDOUT:
        problems.append(f"o stdout mudou: {GOLDEN_STDOUT!r} (51285533) -> {stdout!r}")
    return problems, f"sha256 {got} ({len(data)} bytes)"


def control_d(tmp):
    problems = []
    base = ["full.h", "out.h", KEEP]
    wrong_line = {
        "--provenance sem valor": [*base, "--provenance"],
        "--provenance duas vezes": ["--provenance", "a", *base, "--provenance", "b"],
        "--provenance=TEXT": [*base, "--provenance=" + TEXT],
        "um posicional a mais": [*base, "mais", "--provenance", TEXT],
        "um posicional a menos": ["full.h", "out.h", "--provenance", TEXT],
        "sem a opção, um posicional a mais": [*base, "mais"],
        "sem a opção, dois posicionais": ["full.h", "out.h"],
    }
    bad_text = {
        "texto vazio": "",
        "quebra de linha": "abc\ndef",
        "retorno de carro": "abc\rdef",
        "tabulação": "abc\tdef",
        "avanço de página": "abc\x0cdef",
        "DEL": "abc\x7fdef",
        "barra invertida no fim": "abc\\",
        "barra invertida no meio": "abc\\def",
        "fora do ASCII": "ção",
    }
    cases = [(what, args, "Usage:") for what, args in wrong_line.items()]
    cases += [(f"texto com {what}", [*base, "--provenance", text], "--provenance:")
              for what, text in bad_text.items()]
    for what, args, expected in cases:
        d = fresh(tmp)
        p = run(d, *args)
        if p.returncode != 1:
            problems.append(f"{what}: saiu {p.returncode}, esperava 1")
        if expected not in p.stderr:
            problems.append(f"{what}: stderr sem {expected!r}: {last_line(p.stderr)!r}")
        if p.stdout:
            problems.append(f"{what}: stdout devia estar vazio, tem {p.stdout!r}")
        if (d / "out.h").exists():
            problems.append(f"{what}: o arquivo foi criado")
        # Um arquivo que já existia fica como estava.
        (d / "out.h").write_bytes(b"ANTIGO\n")
        run(d, *args)
        if (d / "out.h").read_bytes() != b"ANTIGO\n":
            problems.append(f"{what}: o arquivo que já existia foi alterado")
    return problems, f"{len(cases)} linhas de comando recusadas"


def control_e(tmp):
    spec = importlib.util.spec_from_file_location("subset_font_sob_teste", TOOL)
    module = importlib.util.module_from_spec(spec)
    sys.dont_write_bytecode = True  # não escreve arquivos: nem o .pyc do subset_font
    spec.loader.exec_module(module)

    _, bitmaps, glyphs = full_header()
    kept = {FIRST + i for i in range(COUNT) if chr(FIRST + i) in KEEP}
    d = fresh(tmp)
    problems = []
    parsed = {}
    for label, extra in (("sem a opção", []), ("com a opção", ["--provenance", TEXT])):
        p = run(d, "full.h", "out.h", KEEP, *extra)
        if p.returncode != 0:
            problems.append(f"{label}: saiu {p.returncode}; stderr {last_line(p.stderr)!r}")
            continue
        parsed[label] = module.parse_font_header((d / "out.h").read_text(encoding="utf-8"))
    if len(parsed) == 2:
        if parsed["sem a opção"] != parsed["com a opção"]:
            problems.append("parse_font_header lê outra coisa do arquivo com a linha de proveniência")
        _, got_glyphs, bitmap_name, glyph_name, font_name, first, last, line_h = parsed["com a opção"]
        if (bitmap_name, glyph_name, font_name) != (f"{NAME}Bitmaps", f"{NAME}Glyphs", NAME):
            problems.append(f"nomes lidos: {(bitmap_name, glyph_name, font_name)!r}")
        if (first, last, line_h) != (FIRST, FIRST + COUNT - 1, LINE_H):
            problems.append(f"faixa e yAdvance lidos: {(first, last, line_h)!r}")
        # Só os glifos que se mantêm têm bitmap e tamanho; os outros guardam o xAdvance e mais nada.
        for i, g in enumerate(got_glyphs):
            off, w, h, xa, xo, yo = glyphs[i]
            want = (g["width"], g["height"]) == ((w, h) if FIRST + i in kept else (0, 0)) and g["xAdvance"] == xa
            if not want:
                problems.append(f"glifo U+{FIRST + i:04X}: lido {g!r}")
    return problems, ""


CONTROLS = [
    ("(a) --provenance grava `// TEXT` como primeira linha e mais nada muda, em qualquer posição", control_a),
    ("(b) controle negativo: sem a opção, a primeira linha é `/*` e o texto não existe", control_b),
    ("(c) sem a opção, o arquivo e o stdout são os do script de antes (51285533)", control_c),
    ("(d) linha de comando errada ou texto que não cabe num comentário: sai 1 e não grava o arquivo", control_d),
    ("(e) parse_font_header lê o mesmo do arquivo com a linha e do arquivo sem ela", control_e),
]


def main():
    if not TOOL.is_file():
        print(f"test_subset_font: falta {TOOL}", file=sys.stderr)
        return 2
    passed = True
    try:
        with tempfile.TemporaryDirectory() as d:
            for name, control in CONTROLS:
                problems, detail = control(Path(d))
                print(("[ok] " if not problems else "[FAIL] ") + name)
                if detail:
                    print("       " + detail)
                for problem in problems:
                    print("       " + problem)
                passed = passed and not problems
    except Setup as e:
        print(f"test_subset_font: o ensaio não se monta: {e}", file=sys.stderr)
        return 2
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
