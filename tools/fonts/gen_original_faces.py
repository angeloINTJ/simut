#!/usr/bin/env python3
"""gen_original_faces.py — regera as três faces do painel original do TTF que o pino fixa, e confere o que está gravado.

As faces do painel (src/ui/original/SansBold*.h) vêm da Liberation Sans Bold (OFL-1.1), cujo TTF fica fora do repositório,
fixado por sha256 em tools/fonts/liberation.json. Esta é a receita delas escrita como código, para que o texto não precise ser
copiado à mão (e não tenha como envelhecer sem ninguém ver): para cada face da tabela FACES, tools/gen_gfx_font.py desenha o
cabeçalho inteiro (FACE.full.h, com --require dos glifos mantidos e --y-advance) e tools/subset_font.py o corta a esses glifos, com a
linha de proveniência (o TTF, a versão e o sha256 do pino) como primeira linha. Os dois passos rodam numa pasta de trabalho
temporária, porque a linha "Source:" do cabeçalho cita o nome relativo do intermediário. O sha256 do TTF e o da linha de
proveniência são lidos do pino na hora, nenhum está copiado aqui, e o TTF só é usado depois de bater com o pino: o script grava os
bytes que conferiu numa cópia e gera dessa cópia, não do caminho.

Uso: gen_original_faces.py [--check] DIR [--headers PASTA]
  DIR      o que `python3 tools/fonts/fetch_liberation.py DIR` preenche: o TTF do pino, no caminho que tem no tarball.
  --check  não grava nada: regera e compara byte a byte com os três cabeçalhos de PASTA.
  --headers PASTA  onde os cabeçalhos estão (e onde a escrita os põe); o padrão é src/ui/original deste repositório. A pasta tem
           de existir. O ensaio (tools/test_gen_original_faces.py) a usa para nunca tocar nos arquivos rastreados.
Saída: 0 se gravou os três (ou, com --check, se os três são iguais ao que o TTF gera); 1 só com --check, se algum difere (stdout:
cada face que difere, a versão do FreeType em uso e a dos cabeçalhos gravados); 2 se não deu para olhar (a linha de comando, o pino
ilegível, o TTF que falta ou não é o do pino, o freetype-py que não carrega, a pasta que não existe, um passo da geração que
falha), com a razão em stderr. O 1 quer dizer que se olhou e os bytes são outros; o 2, que não se chegou a olhar. Quem chama não deve
tratar um pelo outro.

O interpretador precisa de freetype-py==2.5.1 (FreeType 2.13.2 em 2026-11-09): os bits do cabeçalho vêm da rasterização do FreeType, e
outra versão dele pode mudar bits sem que nada neste repositório tenha mudado. Por isso a diferença sai com as duas versões.
"""
import hashlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True  # não escreve em tools/fonts/: nem o .pyc do fetch_liberation
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from fetch_liberation import DEFAULT_PIN, Unverifiable, load_pin  # noqa: E402

TOOLS = HERE.parent
ROOT = TOOLS.parent
GEN = TOOLS / "gen_gfx_font.py"
SUBSET = TOOLS / "subset_font.py"
DEFAULT_HEADERS = ROOT / "src" / "ui" / "original"
PIN_LABEL = DEFAULT_PIN.relative_to(ROOT).as_posix()  # o que a linha de proveniência chama de pino
BOLD = "LiberationSans-Bold.ttf"
# O FreeType de que vieram os cabeçalhos gravados (o do freetype-py 2.5.1, medido em 2026-11-09). É a única coisa daqui que o
# pino não diz: sai na mensagem de diferença, ao lado da versão em uso.
COMMITTED_FREETYPE = "2.13.2"
USO = "Uso: gen_original_faces.py [--check] DIR [--headers PASTA]"

ASCII = "".join(chr(code) for code in range(0x20, 0x7F))
# O que as faces de 9 e 12 pt guardam: o ASCII mais o que o pt-BR e o es-ES pedem (128 caracteres).
LATIN1 = ASCII + "°ºª¿¡ÇçÃãÕõÁáÉéÍíÓóÚúÂâÊêÔôÀàÑñÜü"
# A tabela das faces, uma vez só: símbolo (o nome do cabeçalho e da struct), tamanho em pt, primeiro e último código, o yAdvance
# do cabeçalho que a face substitui (o FreeType mede 20, 27 e 54 na Liberation, e o leiaute do painel foi desenhado com estes) e
# os caracteres mantidos (os demais ficam sem bitmap; a de 24 pt guarda 25, os do relógio e dos rótulos grandes).
FACES = (
    ("SansBold9pt8b_latin1", 9, 0x20, 0xFF, 22, LATIN1),
    ("SansBold12pt8b_latin1", 12, 0x20, 0xFF, 29, LATIN1),
    ("SansBold24pt7b_subset", 24, 0x20, 0x7E, 56, "-.0123456789CEIMORSTUcvor"),
)


class Refused(Exception):
    """Não se chegou a olhar (linha de comando, pino, TTF, freetype, pasta, geração): sai 2."""


def parse_args(argv):
    """(check, DIR, PASTA): um posicional, --check no máximo uma vez e --headers com valor, também no máximo uma vez.

    Sem DIR, DIR a mais, opção desconhecida (qualquer argumento que comece com `-`), --headers sem valor ou repetido, --check
    repetido e a forma --headers=PASTA (uma opção desconhecida) saem 2 com a linha Uso:: uma opção que a linha de comando pula
    em silêncio não confere nada.
    """
    pos, headers, check = [], None, False
    it = iter(argv)
    for arg in it:
        if arg == "--check":
            if check:
                raise Refused(USO)
            check = True
        elif arg == "--headers":
            value = next(it, None)
            if value is None or headers is not None:
                raise Refused(USO)
            headers = value
        elif arg.startswith("-"):
            raise Refused(USO)
        else:
            pos.append(arg)
    if len(pos) != 1:
        raise Refused(USO)
    return check, pos[0], Path(headers) if headers is not None else DEFAULT_HEADERS


def freetype_version():
    """O FreeType que este interpretador carrega, como "2.13.2"; Refused se o freetype-py não carrega."""
    try:
        import freetype
        return ".".join(str(part) for part in freetype.version())
    except (ImportError, OSError, RuntimeError) as e:
        raise Refused(f"freetype-py does not load in this interpreter ({e}): pip install freetype-py==2.5.1 "
                      f"(or run .venv-tools/bin/python, which has it)")


def read_pin():
    """(pino, o nome do LiberationSans-Bold.ttf no pino): Refused se o pino não se lê ou não tem exatamente um."""
    try:
        pin = load_pin(DEFAULT_PIN)
    except Unverifiable as e:
        raise Refused(str(e))
    names = [name for name in pin["files"] if name.rsplit("/", 1)[-1] == BOLD]
    if len(names) != 1:
        raise Refused(f"{DEFAULT_PIN}: names {len(names)} files called {BOLD}, not exactly one")
    return pin, names[0]


def verified_ttf(directory, name, want):
    """Os bytes do TTF de DIR, conferidos contra o sha256 do pino; Refused se falta, não se lê ou não é o do pino."""
    path = Path(directory) / name
    try:
        data = path.read_bytes()
    except FileNotFoundError:
        raise Refused(f"{path}: not there; python3 tools/fonts/fetch_liberation.py {directory} puts it")
    except OSError as e:
        raise Refused(f"{path}: {e.strerror or e}")
    got = hashlib.sha256(data).hexdigest()
    if got != want:
        raise Refused(f"{path}: sha256 {got}, the pin says {want}")
    return data


def run_step(command, cwd):
    """Roda um script deste diretório com este interpretador; Refused se não sai 0. UTF-8 sempre: o texto do cabeçalho não
    muda com o locale de quem roda."""
    try:
        p = subprocess.run([sys.executable, *map(str, command)], cwd=cwd, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=300, env={**os.environ, "PYTHONUTF8": "1"})
    except subprocess.TimeoutExpired:
        raise Refused(f"{Path(command[0]).name} did not finish in 300 s")
    if p.returncode != 0:
        why = (p.stderr.strip().splitlines() or p.stdout.strip().splitlines() or [""])[-1]
        raise Refused(f"{Path(command[0]).name} exited {p.returncode}: {why}")


def generate(ttf_data, provenance):
    """{símbolo: bytes do cabeçalho} das três faces, geradas numa pasta de trabalho temporária."""
    made = {}
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        ttf = work / BOLD
        ttf.write_bytes(ttf_data)  # a cópia dos bytes que acabaram de bater com o pino
        (work / "out").mkdir()
        for symbol, size, first, last, y_advance, kept in FACES:
            full = f"{symbol}.full.h"
            target = work / "out" / f"{symbol}.h"
            run_step([GEN, ttf, size, f"0x{first:02X}", f"0x{last:02X}", symbol, full,
                      "--require", kept, "--y-advance", y_advance], work)
            run_step([SUBSET, full, target, kept, "--provenance", provenance], work)
            made[symbol] = target.read_bytes()
    return made


def first_difference(a, b):
    """A linha (de 1) do primeiro byte em que `a` e `b` diferem; a do fim do menor se um é o começo do outro."""
    at = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
    return a[:at].count(b"\n") + 1


def differences(made, headers):
    """As linhas de relatório das faces cujo cabeçalho em `headers` falta ou difere do gerado; vazio se são iguais."""
    found = []
    for symbol, *_ in FACES:
        path = Path(headers) / f"{symbol}.h"
        try:
            have = path.read_bytes()
        except FileNotFoundError:
            found.append(f"{path.name}: not in {headers}")
            continue
        except OSError as e:
            raise Refused(f"{path}: {e.strerror or e}")
        if have != made[symbol]:
            found.append(f"{path.name}: first difference at line {first_difference(have, made[symbol])}; the file has "
                         f"sha256 {hashlib.sha256(have).hexdigest()} ({len(have)} B), the generated one "
                         f"{hashlib.sha256(made[symbol]).hexdigest()} ({len(made[symbol])} B)")
    return found


def main(argv):
    try:
        check, directory, headers = parse_args(argv)
        if not headers.is_dir():
            raise Refused(f"{headers}: is not a folder")
        in_use = freetype_version()
        pin, name = read_pin()
        ttf = verified_ttf(directory, name, pin["files"][name])
        provenance = f"{BOLD} {pin['version']} sha256 {pin['files'][name]}, SIL OFL 1.1 (pin: {PIN_LABEL})"
        made = generate(ttf, provenance)
        source = f"{BOLD} {pin['version']}"
        if check:
            found = differences(made, headers)
            if not found:
                print(f"gen_original_faces: the {len(FACES)} headers in {headers} are what {source} generates "
                      f"(FreeType {in_use})")
                return 0
            print(f"gen_original_faces: {len(found)} of {len(FACES)} headers in {headers} differ from what {source} generates:")
            for line in found:
                print(f"  {line}")
            print(f"FreeType in use: {in_use}; the committed headers were generated with {COMMITTED_FREETYPE} "
                  f"(freetype-py 2.5.1, 2026-11-09)")
            print("The same FreeType: the difference is in the headers, the pin or the generators, not in the renderer."
                  if in_use == COMMITTED_FREETYPE else
                  "Another FreeType: the difference may be the renderer's; install freetype-py==2.5.1 and run again.")
            return 1
        if in_use != COMMITTED_FREETYPE:
            print(f"gen_original_faces: FreeType {in_use}, not the {COMMITTED_FREETYPE} the committed headers came from: "
                  f"run --check before committing what this wrote", file=sys.stderr)
        written = []
        for symbol, *_ in FACES:
            path = Path(headers) / f"{symbol}.h"
            try:
                path.write_bytes(made[symbol])
            except OSError as e:
                raise Refused(f"{path}: could not write: {e.strerror or e}")
            written.append(path)
        print(f"gen_original_faces: wrote {len(written)} headers to {headers} from {source} (FreeType {in_use})")
        for path in written:
            print(f"  {path.name}  sha256 {hashlib.sha256(made[path.stem]).hexdigest()}  {len(made[path.stem])} B")
        return 0
    except Refused as e:
        text = str(e)
        print(text if text == USO else f"gen_original_faces: {text}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
