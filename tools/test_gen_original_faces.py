#!/usr/bin/env python3
"""test_gen_original_faces.py — o gen_original_faces.py regera os três cabeçalhos do painel original e só diz o que viu.

tools/fonts/gen_original_faces.py DIR regera src/ui/original/SansBold*.h do LiberationSans-Bold.ttf que está em DIR (o que
tools/fonts/fetch_liberation.py preenche), e --check DIR regera numa pasta temporária e compara byte a byte com os cabeçalhos
gravados: sai 0 se são iguais, 1 se alguma face difere (e nomeia só as que diferem), 2 se não deu para olhar (o TTF falta ou não é
o do pino, o freetype não carrega, a linha de comando está errada). Diferença e recusa nunca se confundem. Dez controles, cada um
com o seu negativo; nenhum toca num arquivo rastreado (a pasta dos cabeçalhos é sempre uma cópia, por --headers):

  (a) a árvore como está: --check sai 0, sem nada em stderr, e não cria nem muda arquivo algum (nem em DIR, nem nos cabeçalhos);
  (b) um byte trocado numa cópia de UM cabeçalho, de cada um dos três em separado: sai 1 e o stdout nomeia só aquela face, diz a
      linha do primeiro byte diferente e a versão do FreeType em uso e a que gerou os cabeçalhos, e o --check não regrava a cópia;
      a cópia sem a troca sai 0 (o que isola a cópia da diferença);
  (c) o FreeType em uso vem do próprio freetype-py, lido na hora (um freetype.py plantado que diz 9.9.9 e, no resto, é o de
      verdade): a cópia sem troca sai 0 (o que decide é o byte, não a versão) e diz 9.9.9; com um byte trocado sai 1 e diz 9.9.9 em
      uso e 2.13.2 nos cabeçalhos; a escrita avisa em stderr que a versão não é a dos cabeçalhos;
  (d) o TTF com um byte trocado: sai 2, a mensagem diz o sha256 e o pino, nada é dito sobre os cabeçalhos, e a escrita não grava;
  (e) DIR vazio ou que não existe: sai 2, no --check e na escrita, e a pasta dos cabeçalhos fica como estava;
  (f) a escrita numa pasta vazia: sai 0 e a pasta passa a ter exatamente os três arquivos, byte a byte iguais aos do repositório
      (o sha256 que o ensaio lê, e o que o script diz de cada um); numa pasta com arquivos velhos também; e --check contra a pasta
      escrita sai 0;
  (g) sem o módulo freetype (um freetype.py plantado que falha ao importar): sai 2 e a mensagem manda instalar o freetype-py;
  (h) a linha de proveniência vem do pino, lido na hora: numa cópia dos scripts com um pino que traz outra versão e o sha256 de um
      TTF com um byte a mais no fim (que não muda glifo algum), a escrita sai 0 e cada arquivo é o do repositório com só a primeira
      linha trocada; --check contra os cabeçalhos do repositório sai 1 e nomeia as três faces;
  (i) a linha de comando errada (sem DIR, DIR a mais, opção desconhecida, --headers sem valor, repetido, em `=`, que não é uma
      pasta, --check repetido): sai 2, com a razão em stderr, nada em stdout e nada gravado;
  (j) no fim, os três cabeçalhos e o pino têm o mesmo sha256 que no começo.

Uso: .venv-tools/bin/python tools/test_gen_original_faces.py --dir DIR
     DIR é o que `python3 tools/fonts/fetch_liberation.py DIR` preenche (o TTF do pino, no caminho que tem no tarball). O
     interpretador precisa de freetype-py (o script roda com ele mesmo); o ensaio recusa um TTF que não bate com o pino.
Saída: 0 se todos os controles passam, 1 se algum falha, 2 se não dá para montar o ensaio (sem --dir também: o uso).
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
TOOL = HERE / "fonts" / "gen_original_faces.py"
PIN = HERE / "fonts" / "liberation.json"
HEADERS = ROOT / "src" / "ui" / "original"
BOLD = "LiberationSans-Bold.ttf"
# O FreeType de que vieram os cabeçalhos gravados (2026-11-09, o do freetype-py 2.5.1): o script o diz quando algo difere.
COMMITTED_FREETYPE = "2.13.2"
# Um freetype.py que diz outro FreeType e, no resto, é o de verdade: tira a própria pasta do caminho, carrega o pacote e troca
# version( ). Com ele a versão "em uso" difere da dos cabeçalhos, o que o FreeType desta máquina (o mesmo) não deixa ver.
OTHER_FREETYPE = """\
import importlib
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [p for p in sys.path if os.path.abspath(p or os.getcwd()) != here]
del sys.modules[__name__]
importlib.import_module("freetype").version = lambda: (9, 9, 9)
"""


class Setup(Exception):
    """O ensaio não se monta: sai 2, não reprova o gen_original_faces.py."""


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def run(*args, tool=TOOL, env=None):
    return subprocess.run(
        [sys.executable, str(tool), *map(str, args)],
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300, env=env)


def env_with(plant):
    """O ambiente em que a pasta `plant` vem primeiro no caminho de importação: o que ela tem esconde o pacote de verdade."""
    return {**os.environ, "PYTHONPATH": os.pathsep.join(filter(None, [str(plant), os.environ.get("PYTHONPATH")]))}


def last_line(text):
    lines = text.strip().splitlines()
    return lines[-1] if lines else ""


def tree(path):
    """Tudo o que há sob `path` (arquivos e pastas, caminhos relativos); vazio se `path` não existe."""
    path = Path(path)
    return sorted(str(p.relative_to(path)) for p in path.rglob("*")) if path.exists() else []


class Context:
    """O que os controles compartilham: o TTF de DIR, o que o pino diz dele e os cabeçalhos do repositório, lidos aqui."""

    def __init__(self, directory):
        try:
            self.pin = json.loads(PIN.read_text(encoding="utf-8"))
            bolds = [n for n in self.pin["files"] if n.rsplit("/", 1)[-1] == BOLD]
        except (OSError, ValueError, KeyError, TypeError) as e:
            raise Setup(f"não leu o pino {PIN}: {e}")
        if len(bolds) != 1:
            raise Setup(f"o pino nomeia {len(bolds)} arquivos {BOLD}, esperava um")
        self.bold = bolds[0]
        self.want = self.pin["files"][self.bold]
        self.dir = Path(directory).resolve()
        self.ttf = self.dir / self.bold
        try:
            self.ttf_bytes = self.ttf.read_bytes()
        except OSError as e:
            raise Setup(f"não leu {self.ttf}: {e.strerror or e} (python3 tools/fonts/fetch_liberation.py {directory})")
        if sha256(self.ttf_bytes) != self.want:
            raise Setup(f"{self.ttf} tem sha256 {sha256(self.ttf_bytes)}, o pino diz {self.want}")
        faces = sorted(HEADERS.glob("SansBold*.h"))
        if len(faces) != 3:
            raise Setup(f"{HEADERS} tem {len(faces)} cabeçalhos SansBold*.h, esperava 3")
        self.committed = {p.name: p.read_bytes() for p in faces}
        try:
            import freetype
            self.freetype = ".".join(str(n) for n in freetype.version())
        except (ImportError, OSError, RuntimeError) as e:
            raise Setup(f"freetype-py não carrega neste interpretador ({e}); rode com .venv-tools/bin/python")

    def snapshot(self):
        """sha256 dos três cabeçalhos do repositório e do pino: o que o ensaio nunca pode mudar."""
        return {**{n: sha256(HEADERS.joinpath(n).read_bytes()) for n in self.committed}, "pin": sha256(PIN.read_bytes())}

    def copy_headers(self, folder):
        """Uma pasta nova com cópias dos três cabeçalhos do repositório."""
        folder.mkdir()
        for name, data in self.committed.items():
            (folder / name).write_bytes(data)
        return folder

    def ttf_in(self, folder, data):
        """Um DIR de mentira: `folder` com `data` onde o pino diz que o TTF fica."""
        target = folder / self.bold
        target.parent.mkdir(parents=True)
        target.write_bytes(data)
        return folder


def flipped(data):
    """Os mesmos bytes com um trocado, no meio."""
    changed = bytearray(data)
    changed[len(changed) // 2] ^= 0x01
    return bytes(changed)


def control_a(tmp, ctx):
    before = (tree(ctx.dir), tree(HEADERS), ctx.snapshot())
    p = run("--check", ctx.dir)
    problems = []
    if p.returncode != 0:
        problems.append(f"saiu {p.returncode}, esperava 0; stdout {last_line(p.stdout)!r}, stderr {last_line(p.stderr)!r}")
    if p.stderr.strip():
        problems.append(f"stderr devia estar vazio, tem {last_line(p.stderr)!r}")
    if (tree(ctx.dir), tree(HEADERS), ctx.snapshot()) != before:
        problems.append("o --check criou, apagou ou mudou arquivos")
    return problems, last_line(p.stdout)


def control_b(tmp, ctx):
    problems = []
    clean = ctx.copy_headers(tmp / "clean")
    p = run("--check", ctx.dir, "--headers", clean)
    if p.returncode != 0:
        problems.append(f"a cópia sem troca: saiu {p.returncode}, esperava 0; stdout {last_line(p.stdout)!r}")
    for i, name in enumerate(sorted(ctx.committed)):
        copy = ctx.copy_headers(tmp / f"b{i}")
        (copy / name).write_bytes(flipped(ctx.committed[name]))
        p = run("--check", ctx.dir, "--headers", copy)
        symbol = Path(name).stem
        if p.returncode != 1:
            problems.append(f"{name} com um byte trocado: saiu {p.returncode}, esperava 1; stderr {last_line(p.stderr)!r}")
        # A linha do primeiro byte diferente, contada aqui (flipped( ) troca o do meio), e o --check não conserta o que achou.
        line = ctx.committed[name][:len(ctx.committed[name]) // 2].count(b"\n") + 1
        if f"line {line};" not in p.stdout:
            problems.append(f"{name} com um byte trocado: o stdout não diz `line {line};`: {p.stdout.strip()!r}")
        if (copy / name).read_bytes() != flipped(ctx.committed[name]):
            problems.append(f"{name} com um byte trocado: o --check regravou o arquivo")
        named = [Path(n).stem for n in sorted(ctx.committed) if Path(n).stem in p.stdout]
        if named != [symbol]:
            problems.append(f"{name} com um byte trocado: o stdout nomeia {named!r}, esperava só {[symbol]!r}")
        if f"FreeType in use: {ctx.freetype};" not in p.stdout or f"generated with {COMMITTED_FREETYPE} " not in p.stdout:
            problems.append(f"{name} com um byte trocado: o stdout não diz o FreeType em uso ({ctx.freetype}) e o dos "
                            f"cabeçalhos ({COMMITTED_FREETYPE}): {p.stdout.strip()!r}")
    return problems, ""


def control_c(tmp, ctx):
    plant = tmp / "plant"
    plant.mkdir()
    (plant / "freetype.py").write_text(OTHER_FREETYPE)
    env = env_with(plant)
    problems = []
    clean = ctx.copy_headers(tmp / "clean")
    p = run("--check", ctx.dir, "--headers", clean, env=env)
    if p.returncode != 0 or "(FreeType 9.9.9)" not in p.stdout:
        problems.append(f"a cópia sem troca: saiu {p.returncode} (esperava 0, o que decide é o byte) e o stdout é {p.stdout.strip()!r}")
    name = sorted(ctx.committed)[0]
    bad = ctx.copy_headers(tmp / "bad")
    (bad / name).write_bytes(flipped(ctx.committed[name]))
    p = run("--check", ctx.dir, "--headers", bad, env=env)
    if p.returncode != 1:
        problems.append(f"{name} com um byte trocado: saiu {p.returncode}, esperava 1")
    for text in (f"FreeType in use: 9.9.9; the committed headers were generated with {COMMITTED_FREETYPE} ",
                 "Another FreeType"):
        if text not in p.stdout:
            problems.append(f"{name} com um byte trocado: o stdout não diz `{text}`: {p.stdout.strip()!r}")
    out = tmp / "c_out"
    out.mkdir()
    p = run(ctx.dir, "--headers", out, env=env)
    if p.returncode != 0 or "9.9.9" not in p.stderr or COMMITTED_FREETYPE not in p.stderr:
        problems.append(f"escrita: saiu {p.returncode} (esperava 0), stderr {p.stderr.strip()!r}, esperava o aviso das duas versões")
    if any((out / n).read_bytes() != data for n, data in ctx.committed.items() if (out / n).is_file()) or len(tree(out)) != 3:
        problems.append(f"escrita: a pasta tem {tree(out)!r} e os arquivos não são os do repositório")
    return problems, ""


def control_d(tmp, ctx):
    problems = []
    bad = ctx.ttf_in(tmp / "ttf", flipped(ctx.ttf_bytes))
    p = run("--check", bad)
    if p.returncode != 2:
        problems.append(f"--check: saiu {p.returncode}, esperava 2; stdout {last_line(p.stdout)!r}")
    if "sha256" not in p.stderr or "pin" not in p.stderr:
        problems.append(f"--check: a mensagem não diz o sha256 e o pino: {last_line(p.stderr)!r}")
    if p.stdout.strip():
        problems.append(f"--check: stdout devia estar vazio (nada se diz dos cabeçalhos), tem {last_line(p.stdout)!r}")
    out = tmp / "c_out"
    out.mkdir()
    q = run(bad, "--headers", out)
    if q.returncode != 2 or tree(out):
        problems.append(f"escrita: saiu {q.returncode} (esperava 2) e a pasta tem {tree(out)!r}")
    return problems, ""


def control_e(tmp, ctx):
    problems = []
    empty = tmp / "empty"
    empty.mkdir()
    out = tmp / "d_out"
    out.mkdir()
    for what, directory in (("DIR vazio", empty), ("DIR que não existe", tmp / "nao_existe")):
        for mode, args in (("--check", ["--check", directory]), ("escrita", [directory, "--headers", out])):
            p = run(*args)
            if p.returncode != 2:
                problems.append(f"{what}, {mode}: saiu {p.returncode}, esperava 2; stderr {last_line(p.stderr)!r}")
            if BOLD not in p.stderr:
                problems.append(f"{what}, {mode}: a mensagem não cita {BOLD}: {last_line(p.stderr)!r}")
            if p.stdout.strip() or tree(out):
                problems.append(f"{what}, {mode}: stdout {last_line(p.stdout)!r}, a pasta tem {tree(out)!r}")
    return problems, ""


def control_f(tmp, ctx):
    problems = []
    names = sorted(ctx.committed)
    stale = tmp / "e_stale"
    stale.mkdir()
    for name in names:
        (stale / name).write_text("// um cabeçalho velho\n")
    for what, out in (("pasta vazia", tmp / "e_empty"), ("pasta com arquivos velhos", stale)):
        out.mkdir(exist_ok=True)
        p = run(ctx.dir, "--headers", out)
        if p.returncode != 0:
            problems.append(f"{what}: saiu {p.returncode}, esperava 0; stderr {last_line(p.stderr)!r}")
            continue
        if tree(out) != names:
            problems.append(f"{what}: a pasta tem {tree(out)!r}, esperava só {names!r}")
        for name in names:
            written = (out / name).read_bytes() if (out / name).is_file() else b""
            if written != ctx.committed[name]:
                problems.append(f"{what}: {name} tem sha256 {sha256(written)}, o do repositório é {sha256(ctx.committed[name])}")
            if sha256(ctx.committed[name]) not in p.stdout:
                problems.append(f"{what}: o stdout não diz o sha256 de {name} ({sha256(ctx.committed[name])[:12]}...)")
    q = run("--check", ctx.dir, "--headers", tmp / "e_empty")
    if q.returncode != 0:
        problems.append(f"--check contra a pasta escrita: saiu {q.returncode}, esperava 0; stdout {last_line(q.stdout)!r}")
    return problems, "; ".join(f"{n} {sha256(ctx.committed[n])[:12]}" for n in names)


def control_g(tmp, ctx):
    plant = tmp / "plant"
    plant.mkdir()
    (plant / "freetype.py").write_text('raise ImportError("planted")\n')
    env = env_with(plant)
    problems = []
    for mode, args in (("--check", ["--check", ctx.dir]), ("escrita", [ctx.dir, "--headers", tmp])):
        p = run(*args, env=env)
        if p.returncode != 2:
            problems.append(f"{mode}: saiu {p.returncode}, esperava 2; stderr {last_line(p.stderr)!r}")
        if "freetype-py" not in p.stderr:
            problems.append(f"{mode}: a mensagem não manda instalar o freetype-py: {last_line(p.stderr)!r}")
        if p.stdout.strip():
            problems.append(f"{mode}: stdout devia estar vazio, tem {last_line(p.stdout)!r}")
    return problems, ""


def control_h(tmp, ctx):
    # O script lê o pino que está ao lado dele, então uma cópia dos scripts com outro pino é o que o faz ler outro: o ensaio
    # não muda o pino do repositório.
    mini = tmp / "mini"
    (mini / "tools" / "fonts").mkdir(parents=True)
    for rel in ("gen_gfx_font.py", "subset_font.py", "fonts/gen_original_faces.py", "fonts/fetch_liberation.py"):
        shutil.copy(HERE / rel, mini / "tools" / rel)
    longer = ctx.ttf_bytes + b"\0"  # um byte além da última tabela: o FreeType o ignora, e o sha256 muda
    pin = json.loads(json.dumps(ctx.pin))
    pin["version"] = "9.9.9"
    pin["files"][ctx.bold] = sha256(longer)
    (mini / "tools" / "fonts" / "liberation.json").write_text(json.dumps(pin), encoding="utf-8")
    tool = mini / "tools" / "fonts" / "gen_original_faces.py"
    directory = ctx.ttf_in(tmp / "g_ttf", longer)
    first = f"// {BOLD} 9.9.9 sha256 {sha256(longer)}, SIL OFL 1.1 (pin: tools/fonts/liberation.json)"
    problems = []
    out = tmp / "g_out"
    out.mkdir()
    p = run(directory, "--headers", out, tool=tool)
    if p.returncode != 0:
        return [f"escrita: saiu {p.returncode}, esperava 0; stderr {last_line(p.stderr)!r}"], ""
    for name, committed in ctx.committed.items():
        lines = (out / name).read_bytes().split(b"\n") if (out / name).is_file() else []
        if lines[:1] != [first.encode()]:
            problems.append(f"{name}: a primeira linha é {lines[:1]!r}, esperava {first!r} (o pino de outra versão)")
        if lines[1:] != committed.split(b"\n")[1:]:
            problems.append(f"{name}: o resto do arquivo difere do repositório (só a primeira linha devia mudar)")
    q = run("--check", directory, "--headers", HEADERS, tool=tool)
    named = [Path(n).stem for n in sorted(ctx.committed) if Path(n).stem in q.stdout]
    if q.returncode != 1 or named != sorted(Path(n).stem for n in ctx.committed):
        problems.append(f"--check contra os cabeçalhos do repositório: saiu {q.returncode} (esperava 1), nomeou {named!r}")
    # E o TTF do repositório contra esse pino: o sha256 que o pino diz já não é o dele, então não se olha (2, e não 1).
    r = run("--check", ctx.dir, "--headers", HEADERS, tool=tool)
    if r.returncode != 2 or "sha256" not in r.stderr:
        problems.append(f"o TTF do pino de verdade contra o pino alterado: saiu {r.returncode} (esperava 2), "
                        f"stderr {last_line(r.stderr)!r}")
    return problems, ""


def control_i(tmp, ctx):
    problems = []
    out = tmp / "h_out"
    out.mkdir()
    a_file = tmp / "h_file"
    a_file.write_text("nao e uma pasta\n")
    d = ctx.dir
    cases = {
        "sem argumento algum": [],
        "só --check, sem DIR": ["--check"],
        "dois DIR": [d, d],
        "opção desconhecida": [d, "--rapido"],
        "--headers sem valor": [d, "--headers"],
        "--headers duas vezes": [d, "--headers", out, "--headers", out],
        "--headers=PASTA": [d, f"--headers={out}"],
        "--check duas vezes": ["--check", "--check", d],
    }
    for what, args in cases.items():
        p = run(*args)
        if p.returncode != 2 or "Uso:" not in p.stderr or p.stdout.strip() or tree(out):
            problems.append(f"{what}: saiu {p.returncode} (esperava 2), stderr {last_line(p.stderr)!r}, "
                            f"stdout {last_line(p.stdout)!r}, a pasta tem {tree(out)!r}")
    for what, folder in (("--headers que não existe", tmp / "nao_existe"), ("--headers que é um arquivo", a_file)):
        for mode, args in (("--check", ["--check", d, "--headers", folder]), ("escrita", [d, "--headers", folder])):
            p = run(*args)
            if p.returncode != 2 or not p.stderr.strip() or p.stdout.strip():
                problems.append(f"{what}, {mode}: saiu {p.returncode} (esperava 2), stderr {last_line(p.stderr)!r}, "
                                f"stdout {last_line(p.stdout)!r}")
    if a_file.read_text() != "nao e uma pasta\n" or tree(tmp / "nao_existe"):
        problems.append("a escrita mexeu no arquivo ou criou a pasta que não existia")
    return problems, f"{len(cases) + 4} linhas de comando recusadas"


def main():
    ap = argparse.ArgumentParser(description="Controles do gen_original_faces.py (veja a docstring).")
    ap.add_argument("--dir", required=True, help="o que fetch_liberation.py preenche: o TTF do pino, no caminho que tem no tarball")
    args = ap.parse_args()
    if not TOOL.is_file():
        print(f"test_gen_original_faces: falta {TOOL}", file=sys.stderr)
        return 2
    try:
        ctx = Context(args.dir)
    except Setup as e:
        print(f"test_gen_original_faces: o ensaio não se monta: {e}", file=sys.stderr)
        return 2
    start = ctx.snapshot()

    controls = [
        ("(a) a árvore como está: --check sai 0 e não muda arquivo algum", control_a),
        ("(b) um byte trocado num cabeçalho (cada um dos três): sai 1 e nomeia só a face", control_b),
        ("(c) o FreeType em uso é o que o freetype-py diz na hora, e só o byte decide", control_c),
        ("(d) o TTF com um byte trocado: sai 2, diz o sha256 e o pino, e não grava", control_d),
        ("(e) DIR vazio ou que não existe: sai 2 e não grava", control_e),
        ("(f) a escrita: os três arquivos saem byte a byte iguais aos do repositório", control_f),
        ("(g) sem o módulo freetype: sai 2 e manda instalar o freetype-py", control_g),
        ("(h) a primeira linha vem do pino, lido na hora (versão e sha256 do TTF)", control_h),
        ("(i) a linha de comando errada: sai 2, com a razão, e não grava", control_i),
    ]
    passed = True
    with tempfile.TemporaryDirectory() as d:
        for i, (name, control) in enumerate(controls):
            sub = Path(d) / f"c{i}"
            sub.mkdir()
            problems, detail = control(sub, ctx)
            print(("[ok] " if not problems else "[FAIL] ") + name)
            if detail:
                print("       " + detail)
            for problem in problems:
                print("       " + problem)
            passed = passed and not problems
    changed = [k for k, v in ctx.snapshot().items() if start.get(k) != v]
    print(("[ok] " if not changed else "[FAIL] ") + "(j) o ensaio não mudou os cabeçalhos nem o pino do repositório")
    if changed:
        print(f"       mudaram: {changed!r}")
    return 0 if passed and not changed else 1


if __name__ == "__main__":
    sys.exit(main())
