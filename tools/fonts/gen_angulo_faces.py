#!/usr/bin/env python3
"""gen_angulo_faces.py — regera as quatro faces de texto do driver Ângulo dos TTF da Liberation que o pino fixa, e confere o que está gravado.

As faces SANS9, SANSB9, MONO9 e MONO12 (src/ui/angulo/kit/fontes/) vêm da Liberation 2.1.5 (OFL-1.1), cujos TTF ficam fora do
repositório, fixados por sha256 em tools/fonts/liberation.json. Esta é a receita delas escrita como código, para que o texto não precise ser
copiado à mão (e não tenha como envelhecer sem ninguém ver). Para cada face da tabela FACES, tools/fonts/gen_angulo_face.py rasteriza o TTF no corpo da tabela (141 dpi, o autohinter leve
do FreeType, o alfa de gama do Skia e o limiar de 50 %, os códigos 32..176 com a tabela EXTRA de angulo_extra.json), grava o `--y-advance` da
tabela e, como primeira linha, a proveniência (o TTF, a versão e o sha256 do pino). Os dois passos que o painel original faz em dois (gerar o
cabeçalho inteiro e cortá-lo) aqui são um: as quatro faces guardam os 145 glifos. O sha256 do TTF e o da linha de proveniência são lidos do
pino na hora, nenhum está copiado aqui, e o TTF só é usado depois de bater com o pino: o script grava os bytes que conferiu numa cópia e gera
dessa cópia, não do caminho. A interface é a de tools/fonts/gen_original_faces.py, que faz o mesmo pelas três faces do painel original.

Uso: gen_angulo_faces.py [--check] DIR [--headers PASTA]
  DIR      o que `python3 tools/fonts/fetch_liberation.py DIR` preenche: os TTF do pino, no caminho que têm no tarball.
  --check  não grava nada: regera e compara byte a byte com os quatro cabeçalhos de PASTA.
  --headers PASTA  onde os cabeçalhos estão (e onde a escrita os põe); o padrão é src/ui/angulo/kit/fontes deste repositório. A pasta tem
           de existir. O ensaio (tools/fonts/test_gen_angulo_face.py) a usa para nunca tocar nos arquivos rastreados.
Saída: 0 se gravou os quatro (ou, com --check, se os quatro são iguais ao que os TTF geram); 1 só com --check, se algum difere (stdout:
cada face que difere, a versão do FreeType em uso e a dos cabeçalhos gravados); 2 se não deu para olhar (a linha de comando, o pino
ilegível, um TTF que falta ou não é o do pino, o freetype-py que não carrega, a pasta que não existe, um passo da geração que falha), com a
razão em stderr. O 1 quer dizer que se olhou e os bytes são outros; o 2, que não se chegou a olhar. Quem chama não deve tratar um pelo outro.

O interpretador precisa de freetype-py==2.5.1 (FreeType 2.13.2 em 2026-11-11): os bits do cabeçalho vêm da rasterização do FreeType, e outra
versão dele pode mudar bits sem que nada neste repositório tenha mudado. Com outro FreeType o script não pula nem desiste: o que decide é o
byte (iguais, sai 0; diferentes, sai 1), e a diferença sai com as duas versões, para quem lê saber onde procurar.
"""
import hashlib
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True  # não escreve em tools/fonts/: nem o .pyc do fetch_liberation
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from fetch_liberation import DEFAULT_PIN, Unverifiable, load_pin  # noqa: E402
# O que as duas receitas fazem igual: o pino, o TTF conferido, o passo que roda um script, a linha da primeira diferença, a versão do FreeType.
from gen_original_faces import Refused, first_difference, freetype_version, run_step, verified_ttf  # noqa: E402

TOOLS = HERE.parent
ROOT = TOOLS.parent
GEN = HERE / "gen_angulo_face.py"
DEFAULT_HEADERS = ROOT / "src" / "ui" / "angulo" / "kit" / "fontes"
PIN_LABEL = DEFAULT_PIN.relative_to(ROOT).as_posix()  # o que a linha de proveniência chama de pino
# O FreeType de que vieram os cabeçalhos gravados (o do freetype-py 2.5.1, medido em 2026-11-11). É a única coisa daqui que o pino não diz:
# sai na mensagem de diferença, ao lado da versão em uso.
COMMITTED_FREETYPE = "2.13.2"
USO = "Uso: gen_angulo_faces.py [--check] DIR [--headers PASTA]"

# A tabela das faces, uma vez só: símbolo (o nome do cabeçalho e da struct), TTF da Liberation (a sans regular, a sans negrito e a mono
# negrito), tamanho em pt e o yAdvance. Os corpos 9, 9, 9 e 12 são aqueles com que o leiaute das telas foi desenhado, e o yAdvance 22, 22,
# 22 e 29 é a regra da entrega, round(pt * 141 / 72 * 1,25), que o gerador aplicaria sozinho; a tabela o diz para que mudar o corpo não
# mude o yAdvance sem ninguém ver.
FACES = (
    ("SANS9", "LiberationSans-Regular.ttf", 9, 22),
    ("SANSB9", "LiberationSans-Bold.ttf", 9, 22),
    ("MONO9", "LiberationMono-Bold.ttf", 9, 22),
    ("MONO12", "LiberationMono-Bold.ttf", 12, 29),
)


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


def read_pin():
    """(pino, {TTF: nome no pino}): Refused se o pino não se lê ou não nomeia exatamente um arquivo para cada TTF da tabela."""
    try:
        pin = load_pin(DEFAULT_PIN)
    except Unverifiable as e:
        raise Refused(str(e))
    names = {}
    for ttf in dict.fromkeys(face[1] for face in FACES):
        found = [name for name in pin["files"] if name.rsplit("/", 1)[-1] == ttf]
        if len(found) != 1:
            raise Refused(f"{DEFAULT_PIN}: names {len(found)} files called {ttf}, not exactly one")
        names[ttf] = found[0]
    return pin, names


def provenance(ttf, pin, name):
    """A primeira linha do cabeçalho de uma face (sem o `// `): o TTF, a versão e o sha256 do pino, e a licença."""
    return f"{ttf} {pin['version']} sha256 {pin['files'][name]}, SIL OFL 1.1 (pin: {PIN_LABEL})"


def load(directory):
    """(TTF, proveniências, pino): os bytes de cada TTF de DIR, conferidos contra o pino, e a linha de proveniência de cada um.

    Refused se o pino não se lê, se um TTF falta, não se lê ou não é o do pino. As duas tabelas são por nome de TTF."""
    pin, names = read_pin()
    ttfs, provenances = {}, {}
    for ttf, name in names.items():
        ttfs[ttf] = verified_ttf(directory, name, pin["files"][name])
        provenances[ttf] = provenance(ttf, pin, name)
    return ttfs, provenances, pin


def generate(ttfs, provenances, gen=GEN):
    """{símbolo: bytes do cabeçalho} das quatro faces, geradas numa pasta de trabalho temporária por `gen`, o gerador de uma face."""
    made = {}
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        for ttf, data in ttfs.items():
            (work / ttf).write_bytes(data)  # a cópia dos bytes que acabaram de bater com o pino
        for symbol, ttf, size, y_advance in FACES:
            target = work / f"{symbol}.h"
            run_step([gen, work / ttf, size, symbol, target, "--y-advance", y_advance, "--provenance", provenances[ttf]], work)
            made[symbol] = target.read_bytes()
    return made


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
        ttfs, provenances, pin = load(directory)
        made = generate(ttfs, provenances)
        source = f"Liberation {pin['version']}"
        if check:
            found = differences(made, headers)
            if not found:
                print(f"gen_angulo_faces: the {len(FACES)} headers in {headers} are what {source} generates (FreeType {in_use})")
                return 0
            print(f"gen_angulo_faces: {len(found)} of {len(FACES)} headers in {headers} differ from what {source} generates:")
            for line in found:
                print(f"  {line}")
            print(f"FreeType in use: {in_use}; the committed headers were generated with {COMMITTED_FREETYPE} "
                  f"(freetype-py 2.5.1, 2026-11-11)")
            print("The same FreeType: the difference is in the headers, the pin or the generators, not in the renderer."
                  if in_use == COMMITTED_FREETYPE else
                  "Another FreeType: the difference may be the renderer's; install freetype-py==2.5.1 and run again.")
            return 1
        if in_use != COMMITTED_FREETYPE:
            print(f"gen_angulo_faces: FreeType {in_use}, not the {COMMITTED_FREETYPE} the committed headers came from: "
                  f"run --check before committing what this wrote", file=sys.stderr)
        written = []
        for symbol, *_ in FACES:
            path = Path(headers) / f"{symbol}.h"
            try:
                path.write_bytes(made[symbol])
            except OSError as e:
                raise Refused(f"{path}: could not write: {e.strerror or e}")
            written.append(path)
        print(f"gen_angulo_faces: wrote {len(written)} headers to {headers} from {source} (FreeType {in_use})")
        for path in written:
            print(f"  {path.name}  sha256 {hashlib.sha256(made[path.stem]).hexdigest()}  {len(made[path.stem])} B")
        return 0
    except Refused as e:
        text = str(e)
        print(text if text == USO else f"gen_angulo_faces: {text}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
