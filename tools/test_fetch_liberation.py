#!/usr/bin/env python3
"""test_fetch_liberation.py — o fetch_liberation.py só entrega o que bate com o pino, e recusa o resto.

tools/fonts/fetch_liberation.py lê o pino (tools/fonts/liberation.json), obtém o tarball (do URL do pino, ou de
--source, um URL https ou um caminho local), confere o sha256 do tarball ANTES de abri-lo e o de cada TTF do pino
contra os bytes que saem dele, e só então grava os TTF em DEST. Divergência sai 1 e não grava nada; o que não se
consegue conferir sai 2. Sete controles; os quatro primeiros usam um tarball e um pino montados aqui (rápidos, sem
rede e sem o arquivo de 2,4 MB), os outros três o tarball oficial e o pino de verdade:

  (a) o que bate: sai 0, grava os TTF do pino com o caminho que têm no tarball e os bytes exatos, lista um caminho por
      linha no stdout e NÃO grava o que o pino não nomeia (o LICENSE que vai no mesmo tarball);
  (b) o tarball diverge do pino (um byte trocado, ou o hash do pino com um dígito trocado): sai 1, a mensagem diz
      `tarball` e DEST nem é criado;
  (c) um TTF diverge, com o hash do tarball certo para os bytes novos (o controle que isola a conferência do TTF da do
      tarball), e um TTF do pino que falta no tarball: sai 1, a mensagem cita o arquivo e nada é gravado, nem os TTF
      que batiam;
  (d) o que não se confere sai 2 e não grava: a linha de comando (sem DEST, dois DEST, opção desconhecida, --source ou
      --pin sem valor, opção repetida, --source=X), um pino ilegível ou malformado (ausente, JSON inválido, chave que falta, hash
      que não é hex de 64, nome de arquivo com `..`, absoluto ou com barra invertida), uma fonte que não se obtém
      (caminho que não existe, file://, e http:// mesmo quando um servidor da própria máquina responde com o tarball
      certo), um DEST que já é um arquivo e um tarball que bate com o pino mas não abre; um membro do tarball que
      não é arquivo comum (link simbólico) é recusado com 1;
  (e) o pino de verdade e o tarball oficial (--tarball): sai 0 e cada arquivo gravado tem o sha256 que o ensaio lê do
      JSON por conta própria, não pelo fetch_liberation.py;
  (f) o controle negativo do pino de verdade: uma cópia do JSON com um dígito trocado no hash do tarball, e outra com um
      dígito trocado no hash do LiberationSans-Bold.ttf, saem 1 e não gravam nada;
  (g) o URL do pino (--network, opcional, usa a rede): sem --source, sai 0 e grava os mesmos arquivos. Sem a opção o
      controle é pulado, e a saída diz que foi.

Uso: python3 tools/test_fetch_liberation.py --tarball liberation-fonts-ttf-2.1.5.tar.gz [--network]
     O tarball é o do pino (o sha256 dele está em tools/fonts/liberation.json); o ensaio recusa outro.
Saída: 0 se todos os controles passam, 1 se algum falha, 2 se não dá para montar o ensaio.
"""
import argparse
import functools
import hashlib
import http.server
import io
import json
import subprocess
import sys
import tarfile
import tempfile
import threading
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOL = HERE / "fonts" / "fetch_liberation.py"
PIN = HERE / "fonts" / "liberation.json"


class Setup(Exception):
    """O ensaio não se monta: sai 2, não reprova o fetch_liberation.py."""


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def run(*args):
    return subprocess.run(
        [sys.executable, str(TOOL), *map(str, args)],
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)


def last_line(text):
    lines = text.strip().splitlines()
    return lines[-1] if lines else ""


def tree(path):
    """Tudo o que há sob `path` (arquivos e pastas, caminhos relativos); vazio se `path` não existe."""
    path = Path(path)
    return sorted(str(p.relative_to(path)) for p in path.rglob("*")) if path.exists() else []


def tarball(members, extra=()):
    """Um .tar.gz em memória: `members` (nome -> bytes), mais `extra`, os membros que não são arquivos comuns."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        for name, data in members.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
        for info in extra:
            tar.addfile(info)
    return buf.getvalue()


def pin_for(data, members, **changes):
    """O pino de um tarball montado aqui: o hash dele e o de cada membro de `members`."""
    pin = {"version": "9.9.9", "url": "https://example.invalid/x.tar.gz", "tarball_sha256": sha256(data),
           "files": {name: sha256(blob) for name, blob in members.items()}}
    pin.update(changes)
    return pin


def write(path, data):
    Path(path).write_bytes(data if isinstance(data, bytes) else json.dumps(data).encode())
    return Path(path)


FAKE = {
    "pkg-1/A-Bold.ttf": bytes(range(256)) * 40,
    "pkg-1/B-Regular.ttf": b"\x00\x01\x00\x00" + bytes(reversed(range(256))) * 30,
}
LICENSE = {"pkg-1/LICENSE": b"This is not a font.\n"}


def control_a(tmp):
    problems = []
    data = tarball({**FAKE, **LICENSE})
    src, pin, dest = write(tmp / "a.tar.gz", data), write(tmp / "a.json", pin_for(data, FAKE)), tmp / "a_dest"
    p = run(dest, "--source", src, "--pin", pin)
    if p.returncode != 0:
        return [f"saiu {p.returncode}, esperava 0; stderr {last_line(p.stderr)!r}"], ""
    for name, blob in FAKE.items():
        f = dest / name
        if not f.is_file() or f.read_bytes() != blob:
            problems.append(f"{name}: não foi gravado com os bytes do tarball")
    if tree(dest) != sorted({"pkg-1", *FAKE}):
        problems.append(f"DEST tem {tree(dest)!r}, esperava só {sorted({'pkg-1', *FAKE})!r} (o LICENSE não é do pino)")
    if p.stdout.splitlines() != [str(dest / name) for name in FAKE]:
        problems.append(f"stdout {p.stdout.splitlines()!r}, esperava um caminho por linha: {[str(dest / n) for n in FAKE]!r}")
    # DEST que já existe, com outra coisa dentro: os TTF entram e o resto fica.
    keep = tmp / "a_dest2"
    keep.mkdir()
    (keep / "meu.txt").write_text("meu\n")
    q = run(keep, "--source", src, "--pin", pin)
    if q.returncode != 0 or (keep / "meu.txt").read_text() != "meu\n" or not (keep / "pkg-1" / "A-Bold.ttf").is_file():
        problems.append(f"DEST que já existia: saiu {q.returncode}; {tree(keep)!r}")
    return problems, ""


def control_b(tmp):
    problems = []
    data = tarball(FAKE)
    pin = write(tmp / "b.json", pin_for(data, FAKE))
    tampered = bytearray(data)
    tampered[len(tampered) // 2] ^= 0x01
    cases = {
        "um byte do tarball trocado": (write(tmp / "b1.tar.gz", bytes(tampered)), pin),
        "um dígito do hash do tarball, no pino": (
            write(tmp / "b2.tar.gz", data),
            write(tmp / "b2.json", pin_for(data, FAKE, tarball_sha256=flip(sha256(data))))),
    }
    for i, (what, (src, pin_path)) in enumerate(cases.items()):
        dest = tmp / f"b_dest{i}"
        p = run(dest, "--source", src, "--pin", pin_path)
        if p.returncode != 1:
            problems.append(f"{what}: saiu {p.returncode}, esperava 1; stderr {last_line(p.stderr)!r}")
        if "tarball" not in p.stderr:
            problems.append(f"{what}: a mensagem não diz `tarball`: {last_line(p.stderr)!r}")
        if dest.exists():
            problems.append(f"{what}: DEST foi criado ({tree(dest)!r})")
        if p.stdout:
            problems.append(f"{what}: stdout devia estar vazio, tem {p.stdout!r}")
    return problems, ""


def flip(hexdigest):
    """O mesmo hash com o primeiro dígito trocado."""
    return ("0" if hexdigest[0] != "0" else "1") + hexdigest[1:]


def control_c(tmp):
    problems = []
    # Um TTF diverge: o tarball com o byte novo tem o hash certo no pino, o TTF não.
    changed = {**FAKE, "pkg-1/B-Regular.ttf": FAKE["pkg-1/B-Regular.ttf"] + b"x"}
    data = tarball(changed)
    pin = pin_for(data, FAKE)  # o hash do tarball é o do tarball novo; os dos TTF são os antigos
    # Um TTF do pino que falta no tarball.
    short = tarball({"pkg-1/A-Bold.ttf": FAKE["pkg-1/A-Bold.ttf"]})
    cases = {
        "um TTF diverge": (write(tmp / "c1.tar.gz", data), write(tmp / "c1.json", pin), "B-Regular.ttf"),
        "um TTF falta no tarball": (write(tmp / "c2.tar.gz", short), write(tmp / "c2.json", pin_for(short, FAKE)),
                                    "B-Regular.ttf"),
    }
    for i, (what, (src, pin_path, named)) in enumerate(cases.items()):
        dest = tmp / f"c_dest{i}"
        p = run(dest, "--source", src, "--pin", pin_path)
        if p.returncode != 1:
            problems.append(f"{what}: saiu {p.returncode}, esperava 1; stderr {last_line(p.stderr)!r}")
        if named not in p.stderr:
            problems.append(f"{what}: a mensagem não cita {named}: {last_line(p.stderr)!r}")
        if dest.exists():
            problems.append(f"{what}: DEST foi criado ({tree(dest)!r}): os que batiam não podem ser gravados")
        if p.stdout:
            problems.append(f"{what}: stdout devia estar vazio, tem {p.stdout!r}")
    return problems, ""


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


class serving:
    """Um servidor http em 127.0.0.1, numa porta livre, que serve os arquivos de `directory` enquanto o `with` dura."""

    def __init__(self, directory):
        handler = functools.partial(Quiet, directory=str(directory))
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)

    def __enter__(self):
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        return f"http://127.0.0.1:{self.server.server_address[1]}"

    def __exit__(self, *exc):
        self.server.shutdown()
        self.server.server_close()


DEST = object()  # o lugar do DEST na linha de comando de um caso: cada caso ganha uma pasta que ainda não existe


def control_d(tmp):
    problems = []
    data = tarball(FAKE)
    src, pin = write(tmp / "d.tar.gz", data), write(tmp / "d.json", pin_for(data, FAKE))
    good = pin_for(data, FAKE)
    n = 0

    def refuse(what, *args, expect=2):
        nonlocal n
        n += 1
        dest = tmp / f"d_dest{n}"
        p = run(*[dest if a is DEST else a for a in args])
        if p.returncode != expect:
            problems.append(f"{what}: saiu {p.returncode}, esperava {expect}; stderr {last_line(p.stderr)!r}")
        if not p.stderr.strip():
            problems.append(f"{what}: nada em stderr")
        if p.stdout:
            problems.append(f"{what}: stdout devia estar vazio, tem {p.stdout!r}")
        if dest.exists():
            problems.append(f"{what}: DEST foi criado")

    def pin_file(what, content):
        return write(tmp / f"d_pin{n + 1}.json", content)

    # A linha de comando.
    refuse("sem DEST", "--source", src, "--pin", pin)
    refuse("dois DEST", DEST, tmp / "outro", "--source", src, "--pin", pin)
    refuse("opção desconhecida", DEST, "--source", src, "--pin", pin, "--rapido")
    refuse("--source sem valor", DEST, "--pin", pin, "--source")
    refuse("--pin sem valor", DEST, "--source", src, "--pin")
    refuse("--source duas vezes", DEST, "--source", src, "--source", src, "--pin", pin)
    refuse("--pin duas vezes", DEST, "--source", src, "--pin", pin, "--pin", pin)
    refuse("--source=CAMINHO", DEST, f"--source={src}", "--pin", pin)
    refuse("--pin=CAMINHO", DEST, "--source", src, f"--pin={pin}")
    # O pino.
    refuse("pino ausente", DEST, "--source", src, "--pin", tmp / "nao_existe.json")
    refuse("pino com JSON inválido", DEST, "--source", src, "--pin", pin_file("json", b"{ isto nao e json"))
    refuse("pino que não é um objeto", DEST, "--source", src, "--pin", pin_file("lista", [1, 2]))
    for key in ("version", "url", "tarball_sha256", "files"):
        refuse(f"pino sem `{key}`", DEST, "--source", src, "--pin",
               pin_file(key, {k: v for k, v in good.items() if k != key}))
    refuse("pino com files vazio", DEST, "--source", src, "--pin", pin_file("vazio", {**good, "files": {}}))
    for what, bad in {"hash do tarball curto": {"tarball_sha256": good["tarball_sha256"][:-1]},
                      "hash do tarball em maiúsculas": {"tarball_sha256": good["tarball_sha256"].upper()},
                      "hash do tarball que não é hex": {"tarball_sha256": "z" * 64}}.items():
        refuse(f"pino com {what}", DEST, "--source", src, "--pin", pin_file(what, {**good, **bad}))
    one = next(iter(good["files"].values()))
    refuse("pino com hash de TTF curto", DEST, "--source", src, "--pin",
           pin_file("ttf", {**good, "files": {"pkg-1/A-Bold.ttf": one[:-1]}}))
    for what, name in {"nome com `..`": "pkg-1/../../fora.ttf", "nome absoluto": "/etc/passwd",
                       "nome com barra invertida": "pkg-1\\A.ttf", "nome vazio": "", "nome com `.`": "pkg-1/./A.ttf",
                       "nome com barra dupla": "pkg-1//A.ttf", "nome que termina em barra": "pkg-1/"}.items():
        refuse(f"pino com {what}", DEST, "--source", src, "--pin", pin_file(what, {**good, "files": {name: one}}))
    # A fonte.
    refuse("fonte que não existe", DEST, "--source", tmp / "nao_existe.tar.gz", "--pin", pin)
    refuse("fonte http://", DEST, "--source", "http://example.invalid/x.tar.gz", "--pin", pin)
    refuse("fonte file://", DEST, "--source", f"file://{src}", "--pin", pin)
    # Um servidor http na própria máquina que serve o tarball certo: só a recusa do esquema impede que o fetch
    # funcione (um host que não existe também sai 2, e não prova nada).
    with serving(tmp) as base:
        refuse("fonte http:// que responde com o tarball certo", DEST, "--source", f"{base}/d.tar.gz", "--pin", pin)
    # DEST que já é um arquivo: não se escreve embaixo dele, e ele fica como estava.
    occupied = write(tmp / "d_occupied", b"MEU\n")
    p = run(occupied, "--source", src, "--pin", pin)
    if p.returncode != 2 or occupied.read_bytes() != b"MEU\n" or "not a directory" not in p.stderr:
        problems.append(f"DEST que é um arquivo: saiu {p.returncode} (esperava 2), arquivo {occupied.read_bytes()!r}, "
                        f"stderr {last_line(p.stderr)!r}")
    # Um tarball que bate com o pino e não abre: o pino descreve outro arquivo que não um tar.gz.
    junk = write(tmp / "d_junk.bin", bytes(range(256)) * 8)
    refuse("tarball que bate com o pino mas não abre", DEST, "--source", junk, "--pin",
           pin_file("junk", pin_for(junk.read_bytes(), FAKE)))
    # Um membro que não é arquivo comum: recusado com 1, e nada gravado. O tarfile lê um link simbólico como o
    # arquivo para onde ele aponta, então o pino traz o hash desse arquivo: sem a recusa o link passaria por TTF.
    link = tarfile.TarInfo("pkg-1/B-Regular.ttf")
    link.type, link.linkname = tarfile.SYMTYPE, "A-Bold.ttf"
    linked = tarball({"pkg-1/A-Bold.ttf": FAKE["pkg-1/A-Bold.ttf"]}, extra=[link])
    through_link = {**FAKE, "pkg-1/B-Regular.ttf": FAKE["pkg-1/A-Bold.ttf"]}
    refuse("membro que é link simbólico", DEST, "--source", write(tmp / "d_link.tar.gz", linked), "--pin",
           pin_file("link", pin_for(linked, through_link)), expect=1)
    return problems, f"{n} linhas de comando e pinos recusados"


def control_e(tmp, tarball_path):
    pin = json.loads(PIN.read_text(encoding="utf-8"))
    dest = tmp / "e_dest"
    p = run(dest, "--source", tarball_path)
    if p.returncode != 0:
        return [f"saiu {p.returncode}, esperava 0; stderr {last_line(p.stderr)!r}"], ""
    problems = []
    for name, want in pin["files"].items():
        f = dest / name
        got = sha256(f.read_bytes()) if f.is_file() else None
        if got != want:
            problems.append(f"{name}: sha256 {got}, o pino diz {want}")
    if p.stdout.splitlines() != [str(dest / name) for name in pin["files"]]:
        problems.append(f"stdout {p.stdout.splitlines()!r}")
    if sorted(p.name for p in dest.rglob("*") if p.is_file()) != sorted(Path(n).name for n in pin["files"]):
        problems.append(f"há mais arquivos em DEST do que os do pino: {tree(dest)!r}")
    return problems, f"{len(pin['files'])} TTF gravados, versão {pin['version']}"


def control_f(tmp, tarball_path):
    pin = json.loads(PIN.read_text(encoding="utf-8"))
    bold = next(n for n in pin["files"] if n.endswith("LiberationSans-Bold.ttf"))
    altered = {
        "o hash do tarball": {**pin, "tarball_sha256": flip(pin["tarball_sha256"])},
        "o hash do LiberationSans-Bold.ttf": {**pin, "files": {**pin["files"], bold: flip(pin["files"][bold])}},
    }
    problems = []
    for i, (what, changed) in enumerate(altered.items()):
        dest = tmp / f"f_dest{i}"
        p = run(dest, "--source", tarball_path, "--pin", write(tmp / f"f{i}.json", changed))
        if p.returncode != 1:
            problems.append(f"{what} trocado: saiu {p.returncode}, esperava 1; stderr {last_line(p.stderr)!r}")
        if dest.exists():
            problems.append(f"{what} trocado: DEST foi criado ({tree(dest)!r})")
    return problems, ""


def control_g(tmp, network):
    if not network:
        return None, "pulado: sem --network o ensaio não usa a rede"
    pin = json.loads(PIN.read_text(encoding="utf-8"))
    dest = tmp / "g_dest"
    p = run(dest)
    if p.returncode != 0:
        return [f"saiu {p.returncode}, esperava 0; stderr {last_line(p.stderr)!r}"], ""
    problems = [f"{name}: sha256 diferente do pino" for name, want in pin["files"].items()
                if not (dest / name).is_file() or sha256((dest / name).read_bytes()) != want]
    return problems, f"baixado de {pin['url']}"


def main():
    ap = argparse.ArgumentParser(description="Controles do fetch_liberation.py (veja a docstring).")
    ap.add_argument("--tarball", required=True, help="o tarball do pino, para os controles (e) e (f)")
    ap.add_argument("--network", action="store_true", help="também baixa do URL do pino (controle g)")
    args = ap.parse_args()
    tarball_path = Path(args.tarball).resolve()
    try:
        pin = json.loads(PIN.read_text(encoding="utf-8"))
        have = sha256(tarball_path.read_bytes())
    except (OSError, ValueError) as e:
        print(f"test_fetch_liberation: não leu o pino ou o tarball: {e}", file=sys.stderr)
        return 2
    if have != pin.get("tarball_sha256"):
        print(f"test_fetch_liberation: {tarball_path} tem sha256 {have}, o pino diz {pin.get('tarball_sha256')}",
              file=sys.stderr)
        return 2
    if not TOOL.is_file():
        print(f"test_fetch_liberation: falta {TOOL}", file=sys.stderr)
        return 2

    controls = [
        ("(a) o que bate com o pino: sai 0, grava só os TTF do pino com os bytes do tarball", lambda t: control_a(t)),
        ("(b) o tarball diverge do pino: sai 1, diz `tarball` e não cria DEST", lambda t: control_b(t)),
        ("(c) um TTF diverge ou falta: sai 1, cita o arquivo e não grava nada", lambda t: control_c(t)),
        ("(d) o que não se confere (linha de comando, pino, fonte, tarball): sai 2 e não grava", lambda t: control_d(t)),
        ("(e) o pino de verdade e o tarball oficial: sai 0 e cada TTF tem o sha256 do JSON", lambda t: control_e(t, tarball_path)),
        ("(f) o pino de verdade com um dígito trocado (tarball, TTF): sai 1 e não grava", lambda t: control_f(t, tarball_path)),
        ("(g) o URL do pino, pela rede (--network)", lambda t: control_g(t, args.network)),
    ]
    passed = True
    with tempfile.TemporaryDirectory() as d:
        for i, (name, control) in enumerate(controls):
            sub = Path(d) / f"c{i}"
            sub.mkdir()
            problems, detail = control(sub)
            tag = "[skip] " if problems is None else ("[ok] " if not problems else "[FAIL] ")
            print(tag + name)
            if detail:
                print("       " + detail)
            for problem in problems or []:
                print("       " + problem)
            passed = passed and not problems
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
