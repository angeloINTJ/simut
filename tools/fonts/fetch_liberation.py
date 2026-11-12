#!/usr/bin/env python3
"""fetch_liberation.py — baixa os TTF da Liberation e só os entrega se batem com o pino.

Lê o pino, tools/fonts/liberation.json: a versão, o URL do tarball oficial, o sha256 do tarball e o sha256 de
cada TTF de que as faces dos dois painéis são geradas (as três do painel original, por tools/fonts/gen_original_faces.py,
e as quatro de texto do Ângulo, por tools/fonts/gen_angulo_faces.py). Obtém o tarball (do URL do pino, ou de
--source, um URL https ou um caminho local), confere o sha256 do TARBALL antes de abri-lo, confere o sha256 de
cada TTF do pino contra os bytes que saem do tarball e só então grava os TTF em DEST, com o caminho que têm no
tarball (DEST/liberation-fonts-ttf-2.1.5/LiberationSans-Bold.ttf). Só os arquivos que o pino nomeia são
gravados. Qualquer divergência (o tarball, um TTF, um TTF que falta ou que não é arquivo comum) sai 1 e não
grava nada, nem os TTF que bateram.

Uso: python3 tools/fonts/fetch_liberation.py DEST [--source URL_OU_CAMINHO] [--pin liberation.json]
Saída: 0 se tudo bate (stdout: um caminho por linha, o de cada TTF gravado), 1 se algo difere do pino, 2 se não
dá para conferir (linha de comando, pino ilegível ou malformado, fonte que não se obtém, tarball que não abre,
DEST que não é uma pasta). A razão vai em stderr.

A recusa (1) quer dizer que os bytes foram lidos e são outros; o 2, que não se chegou a olhar. Quem chama não
deve tratar um pelo outro.
"""
import hashlib
import http.client
import io
import json
import re
import sys
import tarfile
import urllib.error
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_PIN = HERE / "liberation.json"
USO = "Uso: fetch_liberation.py DEST [--source URL_OU_CAMINHO] [--pin liberation.json]"
OPCOES = ("--source", "--pin")
# O tarball do pino tem 2,4 MB (2026-11-09): o que passa de 32 MiB não é o arquivo do pino, e nem se lê inteiro.
MAX_BYTES = 32 * 1024 * 1024
SHA256 = re.compile(r"[0-9a-f]{64}")


class Refused(Exception):
    """Os bytes foram lidos e não são os do pino: sai 1."""


class Unverifiable(Exception):
    """Não se chegou a conferir (linha de comando, pino, fonte, tarball, DEST): sai 2."""


def parse_args(argv):
    """(DEST, source ou None, pin): um posicional, e --source e --pin, cada um com valor e no máximo uma vez.

    Sem DEST, DEST a mais, opção desconhecida (qualquer argumento que comece com `-`), opção sem valor ou
    repetida e a forma --source=X (que é uma opção desconhecida) saem 2 com a linha Uso:.
    """
    pos, found = [], {}
    it = iter(argv)
    for arg in it:
        if arg in OPCOES:
            value = next(it, None)
            if value is None or arg in found:
                raise Unverifiable(USO)
            found[arg] = value
        elif arg.startswith("-"):
            raise Unverifiable(USO)
        else:
            pos.append(arg)
    if len(pos) != 1:
        raise Unverifiable(USO)
    return pos[0], found.get("--source"), found.get("--pin", str(DEFAULT_PIN))


def valid_member(name):
    """Um caminho relativo, em POSIX, sem `.`, `..` nem partes vazias: o que se grava fica sob DEST."""
    return (isinstance(name, str) and name != "" and "\\" not in name and "\0" not in name
            and all(part not in ("", ".", "..") for part in name.split("/")))


def load_pin(path):
    """O pino lido e conferido na forma; Unverifiable se não dá para ler ou se a forma está errada."""
    try:
        with open(path, encoding="utf-8") as f:
            pin = json.load(f)
    except (OSError, ValueError) as e:
        raise Unverifiable(f"{path}: {getattr(e, 'strerror', None) or e}")
    if not isinstance(pin, dict):
        raise Unverifiable(f"{path}: the pin is not a JSON object")
    for key in ("version", "url", "tarball_sha256", "files"):
        if key not in pin:
            raise Unverifiable(f"{path}: the pin has no `{key}`")
    if not (isinstance(pin["version"], str) and pin["version"]):
        raise Unverifiable(f"{path}: `version` is not a text")
    if not (isinstance(pin["url"], str) and pin["url"].startswith("https://")):
        raise Unverifiable(f"{path}: `url` is not an https URL")
    if not (isinstance(pin["tarball_sha256"], str) and SHA256.fullmatch(pin["tarball_sha256"])):
        raise Unverifiable(f"{path}: `tarball_sha256` is not 64 lowercase hex digits")
    files = pin["files"]
    if not (isinstance(files, dict) and files):
        raise Unverifiable(f"{path}: `files` is not a non-empty object")
    for name, digest in files.items():
        if not valid_member(name):
            raise Unverifiable(f"{path}: `files` names {name!r}, which is not a relative path under DEST")
        if not (isinstance(digest, str) and SHA256.fullmatch(digest)):
            raise Unverifiable(f"{path}: the sha256 of {name} is not 64 lowercase hex digits")
    return pin


def read_source(source):
    """Os bytes do tarball: de um URL https ou de um caminho local; Unverifiable se não se obtém."""
    try:
        if "://" in source:
            if not source.startswith("https://"):
                raise Unverifiable(f"{source}: only https URLs are fetched; a file goes by its path")
            with urllib.request.urlopen(source, timeout=60) as response:
                data = response.read(MAX_BYTES + 1)
        else:
            with open(source, "rb") as f:
                data = f.read(MAX_BYTES + 1)
    except (OSError, urllib.error.URLError, http.client.HTTPException) as e:
        raise Unverifiable(f"{source}: {getattr(e, 'strerror', None) or getattr(e, 'reason', None) or e}")
    if len(data) > MAX_BYTES:
        raise Unverifiable(f"{source}: more than {MAX_BYTES} bytes, which is not the tarball of the pin")
    return data


def verified_files(data, pin):
    """{nome: bytes} de cada TTF do pino, conferidos; Refused se o tarball ou um TTF difere, Unverifiable se não abre."""
    got = hashlib.sha256(data).hexdigest()
    if got != pin["tarball_sha256"]:
        raise Refused(f"tarball sha256 {got}, the pin says {pin['tarball_sha256']}")
    try:
        tar = tarfile.open(fileobj=io.BytesIO(data), mode="r:gz")
    except (tarfile.TarError, OSError, EOFError) as e:
        raise Unverifiable(f"the tarball matches the pin and does not open as a .tar.gz: {e}")
    files = {}
    with tar:
        for name, want in pin["files"].items():
            try:
                member = tar.getmember(name)
            except KeyError:
                raise Refused(f"{name}: not in the tarball")
            if not member.isfile():
                raise Refused(f"{name}: not a regular file in the tarball")
            if member.size > MAX_BYTES:
                raise Refused(f"{name}: {member.size} bytes in the tarball, more than {MAX_BYTES}")
            blob = tar.extractfile(member).read()
            digest = hashlib.sha256(blob).hexdigest()
            if digest != want:
                raise Refused(f"{name}: sha256 {digest}, the pin says {want}")
            files[name] = blob
    return files


def main(argv):
    try:
        dest, source, pin_path = parse_args(argv)
        pin = load_pin(pin_path)
        files = verified_files(read_source(source or pin["url"]), pin)
        # Só agora se toca em DEST: tudo o que vai para lá foi conferido.
        dest = Path(dest)
        if dest.exists() and not dest.is_dir():
            raise Unverifiable(f"{dest}: exists and is not a directory")
        written = []
        try:
            for name, blob in files.items():
                target = dest / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(blob)
                written.append(target)
        except OSError as e:
            raise Unverifiable(f"{dest}: could not write {name}: {e.strerror or e}")
    except Refused as e:
        print(f"fetch_liberation: refused: {e}", file=sys.stderr)
        return 1
    except Unverifiable as e:
        text = str(e)
        print(text if text == USO else f"fetch_liberation: {text}", file=sys.stderr)
        return 2
    print(f"fetch_liberation: the tarball and {len(files)} files match the pin (version {pin['version']})",
          file=sys.stderr)
    for target in written:
        print(target)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
