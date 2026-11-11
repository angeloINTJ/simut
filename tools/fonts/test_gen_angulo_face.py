#!/usr/bin/env python3
"""test_gen_angulo_face.py — gen_angulo_face.py e a receita das quatro faces de texto do Ângulo (gen_angulo_faces.py) contra as versionadas.

SANS9, SANSB9, MONO9 e MONO12 (src/ui/angulo/kit/fontes/) são geradas dos TTF da Liberation 2.1.5 que tools/fonts/liberation.json fixa. A receita
(o TTF, o corpo, o yAdvance e a linha de proveniência de cada face) é tools/fonts/gen_angulo_faces.py; gen_angulo_face.py é o gerador de uma
face, que rasteriza com o FreeType. Onze controles:

  extra        angulo_extra.json igual aos comentários `// 0xNN 'c'` dos quatro cabeçalhos e aos code points do kExtra de AnguloMeta.h (não
               precisa de TTF);
  faces        as quatro faces, geradas pela receita dos TTF do pino, são as versionadas byte a byte: o sha256 de cada uma é o do cabeçalho
               gravado. Com outro FreeType nada é pulado: o que decide é o byte, e a diferença sai com a versão em uso e a dos cabeçalhos;
  negativos    mudar o dpi, o limiar ou uma entrada da tabela EXTRA muda os glifos (o bitmap ou a tabela de glifos, não só o
               comentário do cabeçalho, que também cita o dpi), e eles deixam de ser os versionados;
  kcapx        caixa alta e altura de x do gerador iguais às de kCapX (AnguloMeta.h), às da linha 3 do cabeçalho e às dos glifos 0x48 e 0x78
               da tabela que o gerador escreve, lida com parse_font_header de subset_font.py, o leitor com que gen_angulo_meta.py mede;
  yadv         --y-advance N manda no yAdvance e em nada mais; valor ruim sai 2 sem arquivo;
  corpo        <pt> aceita fração, com corpos escolhidos aqui e não os da receita (que pode mudar sem que o gerador mude): 8,5 pt vai ao FreeType
               como 544 (26.6, 64 por ponto) e o comentário diz "8.5 pt"; 9 e 12 seguem o de antes ("9 pt" e "12 pt", 576 e 768); o arquivo da
               linha de comando é o que gerar( ) escreve para o mesmo número; o que não é um decimal de 1 a 255 ("8.", ".5", "8,5", "0", "256",
               "1e1", "-1", "nan", vazio) sai 2 sem arquivo;
  provenance--provenance TEXTO é a primeira linha, e sem ela o arquivo começa pelo comentário da face; texto ruim sai 1 sem arquivo;
  determinismo duas execuções dão bytes iguais, também com FREETYPE_PROPERTIES no ambiente;
  formato      o corpo do cabeçalho gerado (do `#pragma once` em diante) tem a forma do versionado (comparador que ignora bits e números), e o
               comparador morde (nove deformações do versionado, cada uma acusada);
  ausente      TTF sem um glifo exigido é recusado: sai 1, escreve `missing glyphs: U+XXXX` e não cria nem toca o arquivo;
  receita      gen_angulo_faces.py, sem depender do que está gravado: a escrita numa pasta nova dá exatamente os quatro arquivos, cada um o que
               o gerador dá com a linha de proveniência do pino, e --check contra ela sai 0; um byte trocado numa cópia de UM cabeçalho sai 1
               e nomeia só a face; um TTF com um byte trocado, DIR vazio ou que não existe, a linha de comando errada e --headers que não é
               pasta saem 2, sem gravar; com um FreeType que diz outra versão (um freetype.py plantado) o byte decide e as duas versões saem.

--mutants roda o teste contra cópias do gerador e da receita com uma troca cada (MUTANTES e MUTANTES_RECEITA) e exige que caiam exatamente os
controles esperados: um controle que nenhuma troca derruba não morde.

Uso: .venv-tools/bin/python tools/fonts/test_gen_angulo_face.py --fonts DIR [--gen CAMINHO] [--recipe CAMINHO] [--mutants]
     DIR é o que `python3 tools/fonts/fetch_liberation.py DIR` preenche (os TTF do pino, no caminho que têm no tarball). O sha256 de cada
     TTF é lido de liberation.json na hora, e uma divergência sai 2. O interpretador precisa de freetype-py.
Saída: 0 se todos os controles passam, 1 se algum falha, 2 se não dá para rodar.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))
from subset_font import parse_font_header  # noqa: E402

FONTES = REPO / "src" / "ui" / "angulo" / "kit" / "fontes"
META = REPO / "src" / "ui" / "angulo" / "AnguloMeta.h"
EXTRA = HERE / "angulo_extra.json"
GEN = HERE / "gen_angulo_face.py"
RECEITA = HERE / "gen_angulo_faces.py"
# O FreeType de que vieram os cabeçalhos gravados (2026-11-11, o do freetype-py 2.5.1): as mensagens o dizem quando algo difere.
FREETYPE = "2.13.2"
# Um freetype.py que diz outro FreeType e, no resto, é o de verdade: tira a própria pasta do caminho, carrega o pacote e troca version( ).
OTHER_FREETYPE = """\
import importlib
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [p for p in sys.path if os.path.abspath(p or os.getcwd()) != here]
del sys.modules[__name__]
importlib.import_module("freetype").version = lambda: (9, 9, 9)
"""


# ---------------------------------------------------------------- leitura e comparação
def corpo(texto):
    """O cabeçalho do `#pragma once` em diante: o que as faces têm em comum, seja qual for o comentário de proveniência."""
    return texto[texto.index("#pragma once"):]


def layout(texto):
    """A forma de um cabeçalho sem os bits nem os números: o que uma face de outro TTF tem em comum com esta."""
    linhas = texto.split("\n")
    if linhas[-1] != "" or (len(linhas) > 1 and linhas[-2] == ""):
        return ["<o arquivo nao termina em um unico \\n>"]
    saida, fila = [], []

    def fecha():
        if fila:
            bom = all(n == 12 for n in fila[:-1]) and 1 <= fila[-1] <= 12
            saida.append("BITMAP" if bom else "BITMAP-ruim%s" % fila)
            fila.clear()

    for ln in linhas[:-1]:
        if re.fullmatch(r"  (?:0x[0-9A-F]{2}, )*0x[0-9A-F]{2},", ln):
            fila.append(ln.count("0x"))
            continue
        fecha()
        g = re.fullmatch(r"  \{ \d+, \d+, \d+, \d+, -?\d+, -?\d+ \},  // (0x[0-9A-F]{2}) '(.)'", ln)
        saida.append("GLIFO %s %s" % g.groups() if g else re.sub(r"\d+", "N", ln))
    fecha()
    return saida


def deformar(ref):
    """O corpo versionado com um defeito de forma por vez: cada um tem de mudar o layout."""
    yield "bitmap row without its trailing comma", re.sub(
        r"(  (?:0x[0-9A-F]{2}, ){11}0x[0-9A-F]{2}),\n", r"\1\n", ref, count=1)
    yield "bitmap row of 11 bytes in the middle", re.sub(r"(\n  )0x[0-9A-F]{2}, ", r"\1", ref, count=1)
    yield "PROGMEM misspelled", ref.replace("PROGMEM", "PROGMEN", 1)
    yield "no final newline", ref.rstrip("\n")
    yield "two final newlines", ref + "\n"
    yield "glyph label quoted otherwise", ref.replace("// 0x20 ' '", '// 0x20 " "', 1)
    yield "pragma with two spaces", ref.replace("#pragma once", "#pragma  once", 1)
    yield "blank line between the arrays gone", ref.replace("};\n\nconst GFXglyph", "};\nconst GFXglyph", 1)
    yield "glyph row without spaces", re.sub(r"\{ (\d+), (\d+), (\d+), (\d+), (-?\d+), (-?\d+) \}", r"{ \1,\2,\3,\4,\5,\6 }", ref, count=1)


def flipped(dados):
    """Os mesmos bytes com um trocado, no meio."""
    trocado = bytearray(dados)
    trocado[len(trocado) // 2] ^= 0x01
    return bytes(trocado)


def sha(dados):
    return hashlib.sha256(dados).hexdigest()


def arvore(pasta):
    """Os nomes que há sob `pasta`, ordenados; vazio se ela não existe."""
    pasta = Path(pasta)
    return sorted(str(p.relative_to(pasta)) for p in pasta.rglob("*")) if pasta.exists() else []


def ultima(texto):
    linhas = texto.strip().splitlines()
    return linhas[-1] if linhas else ""


# ---------------------------------------------------------------- contexto
def carregar(caminho, nome):
    # Uma receita lê o pino e os ajudantes do que está ao lado dela (fetch_liberation.py, gen_original_faces.py): uma cópia em outra pasta,
    # a de um mutante, tem de ler as cópias dela, e não as que um módulo anterior deixou em sys.modules.
    for irmao in ("fetch_liberation", "gen_original_faces"):
        sys.modules.pop(irmao, None)
    spec = importlib.util.spec_from_file_location("%s_%x" % (nome, abs(hash(str(caminho)))), str(caminho))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class NaoRoda(Exception):
    """O ensaio não se monta (o pino, um TTF, a receita): sai 2, não reprova o gerador."""


class Ctx:
    def __init__(self, gen_path, recipe_path, ttfdir):
        self.gen_path, self.recipe_path, self.dir, self.cache = Path(gen_path), Path(recipe_path), Path(ttfdir), {}
        self.tmp = Path(tempfile.mkdtemp(prefix="gen_angulo_face_test."))
        try:
            self.gen, self.erro = carregar(self.gen_path, "gen_angulo_face"), None
        except Exception as e:  # um gerador que não carrega falha todo controle que o usa
            self.gen, self.erro = None, "%s: %s" % (type(e).__name__, e)
        try:
            self.recipe, self.erro_receita = carregar(self.recipe_path, "gen_angulo_faces"), None
        except Exception as e:
            self.recipe, self.erro_receita = None, "%s: %s" % (type(e).__name__, e)
        self.ttfs = self.prov = self.pin = self.nomes = None
        if self.recipe is not None:
            try:
                # Os sha256 vêm do pino, lido agora: nenhum está copiado neste teste.
                self.ttfs, self.prov, self.pin = self.recipe.load(self.dir)
                self.nomes = self.recipe.read_pin()[1]
            except Exception as e:  # Refused da receita: um TTF que falta, que não é o do pino, o pino ilegível
                shutil.rmtree(self.tmp, ignore_errors=True)
                raise NaoRoda(str(e))

    def faces(self):
        return [f[0] for f in self.recipe.FACES]

    def receita(self, simbolo):
        """(TTF, pt, yAdvance) da face, da tabela da receita."""
        return next((f[1], f[2], f[3]) for f in self.recipe.FACES if f[0] == simbolo)

    def ttf(self, simbolo):
        return str(self.dir / self.nomes[self.receita(simbolo)[0]])

    def pt(self, simbolo):
        return self.receita(simbolo)[1]

    def face(self, simbolo, **kw):
        """(texto, info) da face com os parâmetros da receita e a linha de proveniência do pino; kw muda o que o gerador aceita."""
        chave = (simbolo, tuple(sorted(kw.items())))
        if chave not in self.cache:
            ttf, pt, y_advance = self.receita(simbolo)
            self.cache[chave] = self.gen.gerar(self.ttf(simbolo), pt, simbolo, y_advance=y_advance, provenance=self.prov[ttf], **kw)
        return self.cache[chave]

    def run(self, args, env=None):
        e = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        e.update(env or {})
        return subprocess.run([sys.executable, str(self.gen_path)] + [str(a) for a in args],
                              capture_output=True, text=True, encoding="utf-8", env=e, cwd=str(self.tmp))

    def run_receita(self, args, env=None):
        e = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        e.update(env or {})
        return subprocess.run([sys.executable, str(self.recipe_path)] + [str(a) for a in args],
                              capture_output=True, text=True, encoding="utf-8", errors="replace", env=e, cwd=str(self.tmp), timeout=300)

    def sem_gen(self):
        return ["the generator did not load: %s" % self.erro] if self.gen is None else None

    def sem_receita(self):
        return ["the recipe did not load: %s" % self.erro_receita] if self.recipe is None else None

    def freetype(self):
        return ".".join(str(n) for n in self.gen.freetype.version())


# ---------------------------------------------------------------- controles
def c_extra(c):
    p = []
    ent = json.loads(EXTRA.read_text(encoding="utf-8"))["entries"]
    if [e["code"] for e in ent] != list(range(0x7F, 0xB1)):
        p.append("angulo_extra.json: the codes are not 0x7F..0xB0 in order")
    for e in ent:
        if len(e["char"]) != 1 or e["unicode"] != "U+%04X" % ord(e["char"]):
            p.append("angulo_extra.json: code %d: char and unicode disagree" % e["code"])
    tabela = {e["code"]: e["char"] for e in ent}
    for s in c.faces():
        txt = (FONTES / (s + ".h")).read_text(encoding="utf-8")
        h = {int(a, 16): ch for a, ch in re.findall(r"\},\s+// 0x([0-9A-F]{2}) '(.)'\n", txt) if int(a, 16) >= 0x7F}
        if h != tabela:
            p.append("%s.h: the // 0xNN comments differ from angulo_extra.json at %s"
                     % (s, sorted(k for k in set(h) | set(tabela) if h.get(k) != tabela.get(k))))
    meta = META.read_text(encoding="utf-8")
    cp = {int(a, 16): int(u, 16) for u, a in re.findall(r"^\s*0x([0-9a-fA-F]{4}),\s*// 0x([0-9A-F]{2})\b", meta, re.M)}
    if cp != {k: ord(v) for k, v in tabela.items()}:
        p.append("AnguloMeta.h: kExtra differs from angulo_extra.json")
    return p


def c_faces(c):
    if c.sem_gen() or c.sem_receita():
        return c.sem_gen() or c.sem_receita()
    made = c.recipe.generate(c.ttfs, c.prov, gen=c.gen_path)
    p = []
    for s in c.faces():
        have = (FONTES / (s + ".h")).read_bytes()
        if made[s] != have:
            p.append("%s: the generated header is not the versioned one: first difference at line %d; the file has sha256 %s (%d B), the "
                     "generated one %s (%d B)" % (s, c.recipe.first_difference(have, made[s]), sha(have), len(have), sha(made[s]), len(made[s])))
    if p:
        p.append("FreeType in use: %s; the versioned faces were generated with %s%s" % (
            c.freetype(), FREETYPE, "" if c.freetype() == FREETYPE else " (another FreeType: install freetype-py==2.5.1 and run again)"))
    return p


def glifos(texto):
    """O que a rasterização decide num cabeçalho: os bytes do bitmap e a tabela de glifos, sem os comentários."""
    bitmap, tabela = parse_font_header(texto)[:2]
    return bitmap, tabela


def c_negativos(c):
    if c.sem_gen():
        return c.sem_gen()
    p = []
    # Os glifos, não o texto: o dpi também sai no comentário do cabeçalho ("9 pt a 141 dpi"), e um gerador que ignorasse o dpi
    # ao rasterizar mudaria só esse comentário. Comparando o texto, o mutante "the dpi parameter is ignored" passava (2026-11-11).
    base = glifos(c.face("SANS9")[0])
    versionado = glifos((FONTES / "SANS9.h").read_text(encoding="utf-8"))
    perturbacoes = {"dpi + 9": {"dpi": c.gen.DPI + 9}, "alpha threshold + 40": {"limiar": c.gen.ALFA_MIN + 40}}
    dados = json.loads(EXTRA.read_text(encoding="utf-8"))
    e = dados["entries"]
    for k in ("char", "unicode"):
        e[0][k], e[1][k] = e[1][k], e[0][k]
    (c.tmp / "extra_trocada.json").write_text(json.dumps(dados), encoding="utf-8")
    perturbacoes["two EXTRA entries swapped"] = {"extra": str(c.tmp / "extra_trocada.json")}
    for nome, kw in perturbacoes.items():
        g = glifos(c.face("SANS9", **kw)[0])
        if g == base:
            p.append("%s: the glyphs did not change (the bitmap and the glyph table are the same)" % nome)
        elif g == versionado:
            p.append("%s: the glyphs changed and are still the versioned ones: the comparison would not notice" % nome)
    return p


def c_kcapx(c):
    if c.sem_gen():
        return c.sem_gen()
    k = {s: (int(a), int(b)) for a, b, s in re.findall(
        r"\{\s*(\d+),\s*(\d+)\s*\},\s*// (SANS9|SANSB9|MONO9|MONO12)\b", META.read_text(encoding="utf-8"))}
    p = []
    for s in c.faces():
        texto, info = c.face(s)
        medido = (info["cap"], info["xh"], info["digito"])
        # Os mesmos glifos na tabela que o gerador escreveu, lidos como gen_angulo_meta.py os lê.
        _, glifos, _, _, _, primeiro, _, _ = parse_font_header(texto)
        lido = tuple(glifos[ord(ch) - primeiro]["height"] for ch in "Hx0")
        linha3 = tuple(int(x) for x in re.search(
            r"Caixa alta (\d+) px, altura de x (\d+) px, digito (\d+) px",
            (FONTES / (s + ".h")).read_text(encoding="utf-8")).groups())
        if lido != medido:
            p.append("%s: the generator measured cap/x/digit %s, the glyph table it wrote says %s" % (s, medido, lido))
        if k.get(s) != medido[:2]:
            p.append("%s: measured cap/x %s, kCapX %s" % (s, medido[:2], k.get(s)))
        if linha3 != medido:
            p.append("%s: measured cap/x/digit %s, the header's line 3 says %s" % (s, medido, linha3))
    return p


def c_yadv(c):
    if c.sem_gen():
        return c.sem_gen()
    p, d = [], c.tmp / "y"
    d.mkdir()
    base = [c.ttf("SANSB9"), str(c.pt("SANSB9")), "SANSB9", str(d / "base.h")]
    r = c.run(base)
    if r.returncode != 0 or not (d / "base.h").exists():
        return ["no option: exit %s, %s" % (r.returncode, r.stderr.strip()[:200])]
    texto = (d / "base.h").read_text(encoding="utf-8")
    if not texto.endswith(", 32, 176, 22 };\n") or "yAdvance 22" not in r.stdout:
        p.append("no option: the GFXfont line does not end in 22 for 9 pt (or stdout does not say so)")
    for n in (25, 255):
        esperado = texto[:-len("22 };\n")] + "%d };\n" % n
        for pos, args in (("at the end", base + ["--y-advance", n]), ("at the start", ["--y-advance", n] + base),
                          ("in the middle", base[:2] + ["--y-advance", n] + base[2:])):
            r = c.run(args)
            got = (d / "base.h").read_text(encoding="utf-8")
            if r.returncode != 0 or got != esperado or ("yAdvance %d" % n) not in r.stdout:
                p.append("--y-advance %d %s: exit %s, file %s the one without the option with only the last number changed"
                         % (n, pos, r.returncode, "is" if got == esperado else "is not"))
            (d / "base.h").write_text(texto, encoding="utf-8")
    ruins = [["--y-advance", v] for v in ("0", "256", "x", "-1", "+5", "٣", "", "2.5")]
    ruins += [["--y-advance"], ["--y-advance", "5", "--y-advance", "6"], ["--y-advance=5"]]
    for extra in ruins:
        (d / "base.h").unlink(missing_ok=True)
        r = c.run(base + extra)
        if r.returncode != 2 or (d / "base.h").exists():
            p.append("%s: exit %s (want 2), file %s" % (extra, r.returncode, "written" if (d / "base.h").exists() else "not written"))
    return p


def c_corpo(c):
    """O corpo, <pt>, aceita fração. Os corpos são escolhidos aqui e não lidos da receita: o controle prova o que o gerador faz com um corpo, e
    a tabela da receita pode mudar sem que ele mude."""
    if c.sem_gen():
        return c.sem_gen()
    p, d = [], c.tmp / "corpo"
    d.mkdir()
    ttf = c.ttf("SANS9")  # só o arquivo do TTF vem da receita
    rotulo, saida, prov = Path(ttf).stem, d / "x.h", "ensaio do corpo"
    # Cada corpo, o que o FreeType tem de receber (26.6: 64 por ponto) e como o comentário o escreve. À mão, são o oráculo; o 9 e o 12 são o que o
    # gerador fazia antes de aceitar fração: pt << 6 e "9 pt", "12 pt".
    esperado = ((8.5, 544, "8.5"), (9, 576, "9"), (12, 768, "12"))
    # (1) o tamanho que chega ao FreeType, lido na chamada: um Face que anota o que o set_char_size recebe
    chamadas, original = [], c.gen.freetype.Face

    class Espiao(original):
        def set_char_size(self, width=0, height=0, hres=72, vres=72):
            chamadas.append((width, height, hres, vres))
            return super().set_char_size(width, height, hres, vres)

    textos = {}
    c.gen.freetype.Face = Espiao
    try:
        for pt, tamanho, escrito in esperado:
            chamadas.clear()
            try:
                textos[pt] = c.gen.gerar(ttf, pt, "SANS9")[0]
            except Exception as e:  # o gerador recusa o corpo: sai dos outros passos, mas o motivo fica dito
                p.append("gerar( %s pt ): raised %s: %s" % (escrito, type(e).__name__, str(e)[:100]))
                continue
            if chamadas != [(tamanho, 0, c.gen.DPI, 0)]:
                p.append("%s pt: FreeType was handed %s, want [(%d, 0, %d, 0)]" % (escrito, chamadas, tamanho, c.gen.DPI))
    finally:
        c.gen.freetype.Face = original
    # (2) o efeito: a 8,5 pt os glifos não são os de 8 (corpo cortado) nem os de 9 (arredondado para cima)
    if 8.5 in textos:
        feito = glifos(textos[8.5])
        for outro in (8, 9):
            if feito == glifos(textos[outro] if outro in textos else c.gen.gerar(ttf, outro, "SANS9")[0]):
                p.append("8.5 pt: the glyphs are the %d pt ones (the body was cut or rounded to an integer)" % outro)
    # (3) a linha de comando, sem e com --provenance (os dois comentários do cabeçalho): sai 0, o arquivo é o que gerar( ) escreve para o mesmo
    # número (o "9" da linha de comando é um inteiro, e não "9.0") e a linha da face diz o corpo como o oráculo o escreve
    for pt, _, escrito in esperado:
        for com, extra in (("without --provenance", []), ("with --provenance", ["--provenance", prov])):
            saida.unlink(missing_ok=True)
            r = c.run([ttf, escrito, "SANS9", saida] + extra)
            if r.returncode != 0 or not saida.exists():
                p.append("%s pt on the command line, %s: exit %s (want 0), %s" % (escrito, com, r.returncode, ultima(r.stderr)))
                continue
            tem = saida.read_text(encoding="utf-8")
            linha = next((x for x in tem.split("#pragma")[0].split("\n") if x.startswith("// SANS9: ")), "")
            quer = "// SANS9: %s %s pt a %d dpi, " % (rotulo, escrito, c.gen.DPI)
            if not linha.startswith(quer):
                p.append("%s pt on the command line, %s: the face's comment is %r, want it to start with %r" % (escrito, com, linha[:60], quer))
            try:
                igual = tem == c.gen.gerar(ttf, pt, "SANS9", provenance=extra[1] if extra else None)[0]
            except Exception:  # o mesmo número já foi recusado em (1)
                continue
            if not igual:
                p.append("%s pt on the command line, %s: the file is not what gerar( %s ) writes" % (escrito, com, escrito))
    # (4) o que não é um decimal de 1 a 255 sai 2 sem arquivo; o 2 é o da linha de comando, não o 1 de um corpo que o gerador recusa já lido
    for ruim in ("8.", ".5", "8,5", "0", "256", "1e1", "-1", "nan", "", "+8", "٣"):
        saida.unlink(missing_ok=True)
        r = c.run([ttf, ruim, "SANS9", saida])
        if r.returncode != 2 or saida.exists():
            p.append("body %r: exit %s (want 2), file %s" % (ruim, r.returncode, "written" if saida.exists() else "not written"))
    return p


def c_provenance(c):
    if c.sem_gen():
        return c.sem_gen()
    p, d = [], c.tmp / "v"
    d.mkdir()
    base = [c.ttf("SANSB9"), str(c.pt("SANSB9")), "SANSB9", str(d / "x.h")]
    texto = "LiberationSans-Bold.ttf 2.1.5 sha256 788abee4, SIL OFL 1.1 (pin: tools/fonts/liberation.json)"
    if c.run(base).returncode != 0:
        return ["no option: the run failed"]
    sem = (d / "x.h").read_text(encoding="utf-8")
    if not sem.startswith("// SANSB9: LiberationSans-Bold 9 pt a ") or "gerar_fontes_gfx.js" not in sem.split("#pragma")[0]:
        p.append("without --provenance the file does not start with the face's own comment")
    cauda = sem[sem.index("#pragma"):]
    for pos, args in (("at the end", base + ["--provenance", texto]), ("at the start", ["--provenance", texto] + base),
                      ("in the middle", base[:2] + ["--provenance", texto] + base[2:])):
        (d / "x.h").unlink(missing_ok=True)
        r = c.run(args)
        com = (d / "x.h").read_text(encoding="utf-8") if (d / "x.h").exists() else ""
        linhas = com.split("\n")
        if r.returncode != 0 or linhas[0] != "// " + texto:
            p.append("--provenance %s: exit %s, first line %r" % (pos, r.returncode, linhas[0][:80]))
            continue
        cab = com.split("#pragma")[0]
        if not linhas[1].startswith("// SANSB9: LiberationSans-Bold 9 pt") or "gen_angulo_face.py" not in cab or "gerar_fontes_gfx.js" in cab:
            p.append("--provenance %s: the comment block does not name gen_angulo_face.py instead of the delivery's generator" % pos)
        if com[com.index("#pragma"):] != cauda:
            p.append("--provenance %s: something besides the comment block changed" % pos)
    for ruim in ("two\nlines", "acentuação", "back\\slash", ""):
        (d / "x.h").write_text("sentinel", encoding="utf-8")
        r = c.run(base + ["--provenance", ruim])
        if r.returncode != 1 or (d / "x.h").read_text(encoding="utf-8") != "sentinel":
            p.append("--provenance %r: exit %s (want 1) or the file was touched" % (ruim, r.returncode))
    for args in (base + ["--provenance"], base + ["--provenance", "a", "--provenance", "b"], base + ["--provenance=a"]):
        (d / "x.h").unlink(missing_ok=True)
        r = c.run(args)
        if r.returncode != 2 or (d / "x.h").exists():
            p.append("%s: exit %s (want 2) or a file was written" % (args[4:], r.returncode))
    return p


def c_determinismo(c):
    if c.sem_gen():
        return c.sem_gen()
    p, d = [], c.tmp / "d"
    d.mkdir()
    args = [c.ttf("MONO9"), str(c.pt("MONO9")), "MONO9", str(d / "a.h")]
    resultados = []
    for env in (None, None, {"FREETYPE_PROPERTIES": "autofitter:no-stem-darkening=0"},
                {"FREETYPE_PROPERTIES": "truetype:interpreter-version=35"}):
        r = c.run(args, env)
        if r.returncode != 0:
            return ["run failed (env %s): %s" % (env, r.stderr.strip()[:200])]
        resultados.append((hashlib.sha256((d / "a.h").read_bytes()).hexdigest(), r.stdout.replace(str(d), "")))
        (d / "a.h").unlink(missing_ok=True)
    if resultados[0] != resultados[1]:
        p.append("two runs, two different outputs")
    if resultados[0] != resultados[2] or resultados[0] != resultados[3]:
        p.append("FREETYPE_PROPERTIES in the environment changes the output")
    return p


def c_formato(c):
    if c.sem_gen():
        return c.sem_gen()
    ref = corpo((FONTES / "SANS9.h").read_text(encoding="utf-8"))
    p = []
    a, b = layout(ref), layout(corpo(c.face("SANS9")[0]))
    if a != b:
        i = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
        p.append("the generated SANS9 differs in form from the versioned one at token %d: %r against %r" % (i, a[i:i + 1], b[i:i + 1]))
    for nome, mut in deformar(ref):
        if layout(mut) == a:
            p.append("the comparator did not notice: %s" % nome)
    return p


def c_ausente(c):
    if c.sem_gen():
        return c.sem_gen()
    p, d = [], c.tmp / "a"
    d.mkdir()
    dados = json.loads(EXTRA.read_text(encoding="utf-8"))
    for e, ch in zip(dados["entries"][-2:], ("\U0010fffe", "\U0010ffff")):
        e["char"], e["unicode"] = ch, "U+%04X" % ord(ch)
    (d / "extra.json").write_text(json.dumps(dados), encoding="utf-8")
    (d / "vazia.json").write_text("{}", encoding="utf-8")
    saida = d / "x.h"
    base = [c.ttf("SANS9"), str(c.pt("SANS9")), "SANS9", str(saida)]
    r = c.run(base + ["--extra", str(d / "extra.json")])
    if r.returncode != 1 or r.stderr.strip() != "missing glyphs: U+10FFFE U+10FFFF" or saida.exists():
        p.append("two glyphs the TTF lacks: exit %s, stderr %r, file %s" % (r.returncode, r.stderr.strip()[:80], "written" if saida.exists() else "not written"))
    saida.write_text("sentinel", encoding="utf-8")
    c.run(base + ["--extra", str(d / "extra.json")])
    if saida.read_text(encoding="utf-8") != "sentinel":
        p.append("a refused run touched the file that was already there")
    saida.unlink(missing_ok=True)
    r = c.run(base + ["--extra", str(d / "vazia.json")])
    if r.returncode != 1 or saida.exists():
        p.append("a table that is not the EXTRA table: exit %s (want 1) or a file was written" % r.returncode)
    return p


def c_receita(c):
    """gen_angulo_faces.py, em pastas de ensaio: nada do que está gravado entra, e nenhum arquivo rastreado é tocado."""
    if c.sem_gen() or c.sem_receita():
        return c.sem_gen() or c.sem_receita()
    p, d = [], c.tmp / "r"
    d.mkdir()
    simbolos = c.faces()
    nomes = sorted(s + ".h" for s in simbolos)
    # (1) a escrita numa pasta nova: os quatro arquivos, cada um o que o gerador dá com a proveniência do pino
    escrita = d / "escrita"
    escrita.mkdir()
    r = c.run_receita([c.dir, "--headers", escrita])
    if r.returncode != 0 or arvore(escrita) != nomes:
        p.append("write: exit %s, the folder holds %s, expected exactly %s; stderr %r" % (r.returncode, arvore(escrita), nomes, ultima(r.stderr)))
    else:
        for s in simbolos:
            ttf = c.receita(s)[0]
            tem = (escrita / (s + ".h")).read_bytes()
            if tem != c.face(s)[0].encode("utf-8"):
                p.append("write: %s.h is not what the generator gives with the recipe's size, yAdvance and provenance" % s)
            if tem.split(b"\n", 1)[0].decode("utf-8") != "// " + c.prov[ttf] or sha(c.ttfs[ttf]) not in c.prov[ttf]:
                p.append("write: the first line of %s.h is not the pin's provenance of %s" % (s, ttf))
            if sha(tem) not in r.stdout:
                p.append("write: stdout does not say the sha256 of %s.h" % s)
        # (2) --check contra o que a escrita deu: sai 0 e não muda nada
        antes = {n: (escrita / n).read_bytes() for n in nomes}
        q = c.run_receita(["--check", c.dir, "--headers", escrita])
        if q.returncode != 0 or q.stderr.strip() or {n: (escrita / n).read_bytes() for n in nomes} != antes:
            p.append("--check on what the write gave: exit %s, stderr %r, or it changed a file" % (q.returncode, ultima(q.stderr)))
        # (3) um byte trocado numa cópia de UM cabeçalho, de cada um dos quatro: sai 1 e nomeia só a face
        for i, s in enumerate(simbolos):
            cop = d / ("c%d" % i)
            shutil.copytree(escrita, cop)
            mau = flipped(antes[s + ".h"])
            (cop / (s + ".h")).write_bytes(mau)
            q = c.run_receita(["--check", c.dir, "--headers", cop])
            linha = antes[s + ".h"][:len(antes[s + ".h"]) // 2].count(b"\n") + 1
            citados = [t for t in simbolos if t + ".h" in q.stdout]
            if q.returncode != 1 or citados != [s] or "line %d;" % linha not in q.stdout or (cop / (s + ".h")).read_bytes() != mau:
                p.append("%s.h with a byte changed: exit %s (want 1), names %s (want only %s), `line %d;` %s, the copy %s"
                         % (s, q.returncode, citados, s, linha, "in" if "line %d;" % linha in q.stdout else "not in stdout",
                            "was rewritten" if (cop / (s + ".h")).read_bytes() != mau else "was left"))
            if "FreeType in use: %s;" % c.freetype() not in q.stdout or "generated with %s " % FREETYPE not in q.stdout:
                p.append("%s.h with a byte changed: stdout does not say both FreeType versions: %r" % (s, q.stdout.strip()[-200:]))
    # (4) um TTF com um byte trocado: sai 2, diz o sha256 e o pino, nada se diz dos cabeçalhos e a escrita não grava
    ruim = d / "ttf_ruim"
    for ttf, nome in c.nomes.items():
        alvo = ruim / nome
        alvo.parent.mkdir(parents=True, exist_ok=True)
        alvo.write_bytes(flipped(c.ttfs[ttf]) if ttf == "LiberationMono-Bold.ttf" else c.ttfs[ttf])
    vazia = d / "vazia"
    vazia.mkdir()
    q = c.run_receita(["--check", ruim, "--headers", escrita])
    w = c.run_receita([ruim, "--headers", vazia])
    if q.returncode != 2 or "sha256" not in q.stderr or "pin" not in q.stderr or q.stdout.strip():
        p.append("a TTF with a byte changed, --check: exit %s (want 2), stderr %r, stdout %r" % (q.returncode, ultima(q.stderr), ultima(q.stdout)))
    if w.returncode != 2 or arvore(vazia):
        p.append("a TTF with a byte changed, write: exit %s (want 2) and the folder holds %s" % (w.returncode, arvore(vazia)))
    # (5) DIR vazio ou que não existe: sai 2, com o nome do TTF, nos dois modos
    (d / "dir_vazio").mkdir()
    for o_que, pasta in (("an empty DIR", d / "dir_vazio"), ("a DIR that does not exist", d / "nao_existe")):
        for modo, args in (("--check", ["--check", pasta, "--headers", escrita]), ("write", [pasta, "--headers", vazia])):
            q = c.run_receita(args)
            if q.returncode != 2 or "Liberation" not in q.stderr or q.stdout.strip() or arvore(vazia):
                p.append("%s, %s: exit %s (want 2), stderr %r, stdout %r, folder %s" % (o_que, modo, q.returncode, ultima(q.stderr), ultima(q.stdout), arvore(vazia)))
    # (6) a linha de comando errada: sai 2 com a linha Uso:, nada em stdout e nada gravado
    arq = d / "um_arquivo"
    arq.write_text("nao e uma pasta\n")
    casos = {"no argument": [], "--check alone": ["--check"], "two DIR": [c.dir, c.dir], "an unknown option": [c.dir, "--rapido"],
             "--headers without a value": [c.dir, "--headers"], "--headers twice": [c.dir, "--headers", vazia, "--headers", vazia],
             "--headers=PASTA": [c.dir, "--headers=%s" % vazia], "--check twice": ["--check", "--check", c.dir]}
    for o_que, args in casos.items():
        q = c.run_receita(args)
        if q.returncode != 2 or "Uso:" not in q.stderr or q.stdout.strip() or arvore(vazia):
            p.append("%s: exit %s (want 2), stderr %r, stdout %r, folder %s" % (o_que, q.returncode, ultima(q.stderr), ultima(q.stdout), arvore(vazia)))
    for o_que, pasta in (("--headers that does not exist", d / "nao_existe"), ("--headers that is a file", arq)):
        for modo, args in (("--check", ["--check", c.dir, "--headers", pasta]), ("write", [c.dir, "--headers", pasta])):
            q = c.run_receita(args)
            if q.returncode != 2 or not q.stderr.strip() or q.stdout.strip():
                p.append("%s, %s: exit %s (want 2), stderr %r, stdout %r" % (o_que, modo, q.returncode, ultima(q.stderr), ultima(q.stdout)))
    if arq.read_text() != "nao e uma pasta\n" or arvore(d / "nao_existe"):
        p.append("the write touched the file or created the folder that was not there")
    # (7) com outro FreeType o byte decide e as duas versões saem
    plant = d / "plant"
    plant.mkdir()
    (plant / "freetype.py").write_text(OTHER_FREETYPE)
    env = {"PYTHONPATH": os.pathsep.join(filter(None, [str(plant), os.environ.get("PYTHONPATH")]))}
    if c.recipe is not None and r.returncode == 0:
        q = c.run_receita(["--check", c.dir, "--headers", escrita], env)
        if q.returncode != 0 or "(FreeType 9.9.9)" not in q.stdout:
            p.append("another FreeType, a clean copy: exit %s (want 0, the byte decides), stdout %r" % (q.returncode, ultima(q.stdout)))
        cop = d / "c0"
        q = c.run_receita(["--check", c.dir, "--headers", cop], env)
        if q.returncode != 1 or "FreeType in use: 9.9.9; the committed headers were generated with %s " % FREETYPE not in q.stdout \
                or "Another FreeType" not in q.stdout:
            p.append("another FreeType, a copy with a byte changed: exit %s (want 1), stdout %r" % (q.returncode, q.stdout.strip()[-200:]))
        outra = d / "outra"
        outra.mkdir()
        q = c.run_receita([c.dir, "--headers", outra], env)
        if q.returncode != 0 or "9.9.9" not in q.stderr or FREETYPE not in q.stderr or arvore(outra) != nomes:
            p.append("another FreeType, the write: exit %s (want 0), stderr %r, the folder holds %s" % (q.returncode, ultima(q.stderr), arvore(outra)))
    return p


CONTROLES = [("extra", c_extra), ("faces", c_faces), ("negativos", c_negativos), ("kcapx", c_kcapx),
             ("yadv", c_yadv), ("corpo", c_corpo), ("provenance", c_provenance), ("determinismo", c_determinismo),
             ("formato", c_formato), ("ausente", c_ausente), ("receita", c_receita)]


def rodar_controles(gen_path, recipe_path, ttfdir):
    """{controle: a lista de problemas (vazia se passa)}."""
    c = Ctx(gen_path, recipe_path, ttfdir)
    res = {}
    res["_freetype"] = c.freetype() if c.gen is not None else "?"
    try:
        for nome, f in CONTROLES:
            try:
                res[nome] = f(c)
            except Exception as e:  # um controle que levanta é um controle que falha
                res[nome] = ["raised %s: %s" % (type(e).__name__, str(e)[:200])]
    finally:
        shutil.rmtree(c.tmp, ignore_errors=True)
    return res


# ---------------------------------------------------------------- mutantes
# (nome, âncora que aparece uma vez no gerador, troca, controles que têm de cair)
MUTANTES = [
    ('dpi 141 -> 140',
     'DPI = 141',
     'DPI = 140',
     {'faces'}),
    ('the dpi parameter is ignored',
     'face.set_char_size(int(round(pt * 64)), 0, dpi, 0)',
     'face.set_char_size(int(round(pt * 64)), 0, DPI, 0)',
     {'negativos'}),
    ('the threshold parameter is ignored',
     '>= limiar}',
     '>= ALFA_MIN}',
     {'negativos'}),
    ('alpha threshold 128 -> 129',
     'ALFA_MIN = 128',
     'ALFA_MIN = 129',
     {'faces'}),
    ('no gamma table: the coverage is the alpha',
     'y = x * 12.92 if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055',
     'y = x',
     {'faces'}),
    ('no hinting',
     'FLAGS = freetype.FT_LOAD_TARGET_LIGHT | freetype.FT_LOAD_FORCE_AUTOHINT | freetype.FT_LOAD_NO_BITMAP',
     'FLAGS = freetype.FT_LOAD_NO_HINTING | freetype.FT_LOAD_NO_BITMAP',
     {'faces', 'kcapx'}),
    ('xAdvance truncated, not rounded',
     'math.floor(g.linearHoriAdvance / 65536.0 + 0.5)',
     'int(g.linearHoriAdvance / 65536.0)',
     {'faces'}),
    ('bits packed LSB first',
     'b = (b << 1) | (bits[i + k] if i + k < len(bits) else 0)',
     'b = (b >> 1) | ((bits[i + k] if i + k < len(bits) else 0) << 7)',
     {'faces'}),
    ('--y-advance ignored',
     'y_adv = padrao if y_advance is None else y_advance',
     'y_adv = padrao',
     {'yadv'}),
    ('the provenance line is blank',
     'primeira = None if provenance is None else comment_line(provenance)',
     "primeira = None if provenance is None else ''",
     {'receita', 'faces', 'provenance'}),
    ('FREETYPE_PROPERTIES is not cleared',
     "os.environ.pop('FREETYPE_PROPERTIES', None)",
     'pass',
     {'determinismo'}),
    ('a missing glyph is not refused',
     'if faltam:',
     'if False and faltam:',
     {'ausente'}),
    ('12 bytes a row -> 16',
     'LINHA = 12',
     'LINHA = 16',
     {'formato', 'faces'}),
    ('pragma with two spaces',
     "'#pragma once',",
     "'#pragma  once',",
     {'formato', 'faces'}),
    ("the cap height is measured on the 'x'",
     "cap, xh, dig = (por[c]['h'] for c in 'Hx0')",
     "cap, xh, dig = (por[c]['h'] for c in 'xx0')",
     {'faces', 'kcapx'}),
    # O corpo com fração. A receita só tem corpos inteiros enquanto nenhuma face for 8,5: o %d e o int( ) não mudam nada nela, e só `corpo` os vê.
    ('the body is cut to an integer',
     'face.set_char_size(int(round(pt * 64)), 0, dpi, 0)',
     'face.set_char_size(int(pt) << 6, 0, dpi, 0)',
     {'corpo'}),
    ('the comment writes the body with %d (no --provenance)',
     "'// %s: %s %s pt a %d dpi, limiar de 50 %% — gerada pelo mesmo'",
     "'// %s: %s %d pt a %d dpi, limiar de 50 %% — gerada pelo mesmo'",
     {'corpo'}),
    ('the comment writes the body with %d (with --provenance)',
     "'// %s: %s %s pt a %d dpi, limiar de 50 %% — gerada por'",
     "'// %s: %s %d pt a %d dpi, limiar de 50 %% — gerada por'",
     {'corpo'}),
    ('a fraction is refused on the command line',
     r"re.fullmatch(r'[0-9]+(\.[0-9]+)?', pos[1])",
     r"re.fullmatch(r'[0-9]+', pos[1])",
     {'corpo'}),
    ('the body is always read as a float: 9 is written "9.0 pt"',
     'pt = int(pos[1]) if pos[1].isdecimal() else float(pos[1])',
     'pt = float(pos[1])',
     {'corpo', 'faces', 'receita', 'provenance'}),
    ('the body is checked by float() alone: "8.", "1e1", "+8" pass',
     r"re.fullmatch(r'[0-9]+(\.[0-9]+)?', pos[1])",
     'float(pos[1])',
     {'corpo'}),
    ('the range of the body is not checked',
     ' and 1 <= float(pos[1]) <= 255',
     '',
     {'corpo'}),
]
# (nome, âncora que aparece uma vez na receita, troca, controles que têm de cair)
MUTANTES_RECEITA = [
    ('SANS9 gets a yAdvance of 23',
     '("SANS9", "LiberationSans-Regular.ttf", 9, 22),',
     '("SANS9", "LiberationSans-Regular.ttf", 9, 23),',
     {'faces'}),
    ('MONO12 is cut at 11 pt',
     '("MONO12", "LiberationMono-Bold.ttf", 12, 29),',
     '("MONO12", "LiberationMono-Bold.ttf", 11, 29),',
     {'faces', 'kcapx'}),
    ('SANSB9 is made from the Regular TTF',
     '("SANSB9", "LiberationSans-Bold.ttf", 9, 22),',
     '("SANSB9", "LiberationSans-Regular.ttf", 9, 22),',
     {'faces', 'provenance'}),
    ('--check exits 0 when a header differs',
     '            return 1\n        if in_use != COMMITTED_FREETYPE:',
     '            return 0\n        if in_use != COMMITTED_FREETYPE:',
     {'receita'}),
    ('a TTF is used without being checked against the pin',
     '        ttfs[ttf] = verified_ttf(directory, name, pin["files"][name])',
     '        ttfs[ttf] = (Path(directory) / name).read_bytes()',
     {'receita'}),
    ('the provenance line has no sha256',
     'return f"{ttf} {pin[\'version\']} sha256 {pin[\'files\'][name]}, SIL OFL 1.1 (pin: {PIN_LABEL})"',
     'return f"{ttf} {pin[\'version\']}, SIL OFL 1.1 (pin: {PIN_LABEL})"',
     {'receita', 'faces'}),
    ('every face is reported as different',
     '        if have != made[symbol]:',
     '        if True:',
     {'receita'}),
    ('the FreeType of the committed headers is another',
     'COMMITTED_FREETYPE = "2.13.2"',
     'COMMITTED_FREETYPE = "2.13.3"',
     {'receita'}),
]


def _mutante(raiz, onde, src, ancora, troca):
    """Uma árvore mínima em `raiz` com os arquivos de tools/fonts e subset_font.py, e `onde` trocado por src com a troca feita."""
    fonts = raiz / "tools" / "fonts"
    fonts.mkdir(parents=True)
    shutil.copy(TOOLS / "subset_font.py", raiz / "tools" / "subset_font.py")
    for nome in ("angulo_extra.json", "liberation.json", "fetch_liberation.py", "gen_original_faces.py", "gen_angulo_face.py", "gen_angulo_faces.py"):
        shutil.copy(HERE / nome, fonts / nome)
    (fonts / onde).write_text(src.replace(ancora, troca), encoding="utf-8")
    return fonts


def mutantes(args):
    certo, total = True, 0
    for lista, onde in ((MUTANTES, "gen_angulo_face.py"), (MUTANTES_RECEITA, "gen_angulo_faces.py")):
        src = (HERE / onde).read_text(encoding="utf-8")
        for nome, ancora, troca, esperado in lista:
            total += 1
            if src.count(ancora) != 1:
                print("INVALID %s: the anchor occurs %d times in %s" % (nome, src.count(ancora), onde))
                certo = False
                continue
            with tempfile.TemporaryDirectory(prefix="gen_angulo_face_mut.") as d:
                fonts = _mutante(Path(d), onde, src, ancora, troca)
                try:
                    res = rodar_controles(fonts / "gen_angulo_face.py", fonts / "gen_angulo_faces.py", args.fonts)
                except NaoRoda as e:  # uma receita que o próprio mutante quebrou: o ensaio não monta, e isso é um mutante pego por tudo
                    print("FAIL %s: the test could not be set up with it: %s" % (nome, e))
                    certo = False
                    continue
            caiu = {n for n, v in res.items() if v and n != "_freetype"}
            bom = caiu == esperado
            certo = certo and bom
            print("%s %s: fails %s (expected %s)" % ("ok  " if bom else "FAIL", nome, sorted(caiu), sorted(esperado)))
    print("%d mutants, %s" % (total, "all caught by exactly the expected controls" if certo else "SOME NOT CAUGHT AS EXPECTED"))
    return 0 if certo else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--fonts", required=True, help="o que fetch_liberation.py preenche: os TTF do pino, no caminho que têm no tarball")
    ap.add_argument("--gen", default=str(GEN), help="o gerador a testar (um mutante, por exemplo)")
    ap.add_argument("--recipe", default=str(RECEITA), help="a receita a testar (um mutante, por exemplo)")
    ap.add_argument("--mutants", action="store_true")
    args = ap.parse_args()
    try:
        if args.mutants:
            return mutantes(args)
        res = rodar_controles(args.gen, args.recipe, args.fonts)
    except NaoRoda as e:
        print("cannot run: %s" % e, file=sys.stderr)
        return 2
    ruim = 0
    print("FreeType in use: %s (the versioned faces were generated with %s)" % (res["_freetype"], FREETYPE))
    for nome, _ in CONTROLES:
        v = res[nome]
        if v:
            ruim += 1
            print("FAIL %s" % nome)
            for linha in v[:8]:
                print("       " + linha)
        else:
            print("ok   %s" % nome)
    print("%d of %d controls fail" % (ruim, len(CONTROLES)))
    return 1 if ruim else 0


if __name__ == "__main__":
    sys.exit(main())
