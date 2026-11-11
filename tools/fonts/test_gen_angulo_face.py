#!/usr/bin/env python3
"""test_gen_angulo_face.py — gen_angulo_face.py contra as quatro faces de texto que o Ângulo entrega.

SANS9, SANSB9, MONO9 e MONO12 (src/ui/angulo/kit/fontes/) saíram do gerador da entrega do designer
(gerar_fontes_gfx.js, fora do git), que rasteriza com o Skia. gen_angulo_face.py refaz o trabalho com o
FreeType. Este teste prende o que ele reproduz e o que não reproduz. Nove controles:

  extra        angulo_extra.json igual aos comentários `// 0xNN 'c'` dos quatro cabeçalhos e aos code points
               do kExtra de AnguloMeta.h (não precisa de TTF);
  faces        as quatro faces, geradas dos TTF da GNU FreeFont, contra as versionadas: first, last, yAdvance
               e a tabela de códigos iguais; por glifo, xAdvance, largura, altura e deslocamentos. O PISO é o
               que difere hoje (glifos, maior desvio, bits, caixas, glifos que diferem). Byte a byte não
               fecha, e o piso vale por igualdade: se a diferença cair, o piso desce na mesma mudança;
  negativos    mudar o dpi, o limiar ou uma entrada da tabela EXTRA muda a saída, e o piso acusa;
  kcapx        caixa alta e altura de x do gerador iguais às de kCapX (AnguloMeta.h) e às da linha 3 do cabeçalho;
  yadv         --y-advance N manda no yAdvance e em nada mais; valor ruim sai 2 sem arquivo;
  provenance   --provenance TEXTO é a primeira linha, e sem ela o arquivo começa pelo comentário da face;
               texto ruim sai 1 sem arquivo;
  determinismo duas execuções dão bytes iguais, também com FREETYPE_PROPERTIES no ambiente;
  formato      o cabeçalho gerado tem a forma do versionado (comparador que ignora bits e números), e o
               comparador morde (oito deformações do versionado, cada uma acusada);
  ausente      TTF sem um glifo exigido é recusado: sai 1, escreve `missing glyphs: U+XXXX` e não cria nem
               toca o arquivo.

O piso e kCapX valem para o FreeType em que foram medidos (FREETYPE abaixo); com outro, faces e kcapx saem
como SKIP, para não acusar o autohinter de uma versão nova como se fosse o gerador.

--mutants roda o teste contra cópias do gerador com uma troca cada (MUTANTES) e exige que caiam exatamente os
controles esperados: um controle que nenhuma troca derruba não morde.
--measure imprime o que o piso deveria ser, para quem desceu a diferença.

Uso: .venv-tools/bin/python tools/fonts/test_gen_angulo_face.py --freefont DIR [--gen CAMINHO] [--mutants | --measure]
     DIR tem FreeSans.ttf, FreeSansBold.ttf e FreeMonoBold.ttf da GNU FreeFont 20120503 (os sha256 estão em
     SHA256, e uma divergência sai 2). O interpretador precisa de freetype-py.
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

# símbolo -> (TTF, pt)
FACES = {"SANS9": ("FreeSans.ttf", 9), "SANSB9": ("FreeSansBold.ttf", 9),
         "MONO9": ("FreeMonoBold.ttf", 9), "MONO12": ("FreeMonoBold.ttf", 12)}
SHA256 = {"FreeSans.ttf": "c80858440d8fb618e0ac5ff6f16251dbfa6b3316f00f3cdd17d477297dd87b04",
          "FreeSansBold.ttf": "982534a3731416a15e2756601721f26053f68bf4239011550f3dd23ce6308215",
          "FreeMonoBold.ttf": "acd5fbb1985f9c2266c96a995b6cfba981609d598c9ce815388c1d5a8a23962a"}
FREETYPE = (2, 13, 2)
# (glifos com métrica diferente, maior desvio, bits diferentes, glifos de outra caixa, glifos que diferem),
# medidos em 2026-11-11 com FreeType 2.13.2 contra os cabeçalhos versionados no commit a0e9907a.
PISO = {"SANS9": (14, 3, 33, 9, 22), "SANSB9": (11, 3, 19, 11, 16),
        "MONO9": (11, 1, 59, 10, 36), "MONO12": (17, 2, 123, 15, 39)}
CAMPOS = ("width", "height", "xAdvance", "xOffset", "yOffset")


# ---------------------------------------------------------------- leitura e comparação
def ler_face(texto):
    bm, gl, _, _, _, primeiro, ultimo, yadv = parse_font_header(texto)
    rotulos = re.findall(r"^  \{[^}]*\},\s+// 0x([0-9A-F]{2}) '(.)'$", texto, re.M)
    return {"bitmap": bm, "glifos": gl, "primeiro": primeiro, "ultimo": ultimo, "yadv": yadv,
            "rotulos": [(int(c, 16), ch) for c, ch in rotulos]}


def bits_de(bm, g):
    return [(bm[g["offset"] + (i >> 3)] >> (7 - (i & 7))) & 1 for i in range(g["width"] * g["height"])]


def glifos_e_bits(texto):
    r = ler_face(texto)
    return r["glifos"], r["bitmap"], r["rotulos"]


def comparar(ref, ger):
    """O que difere entre a face versionada (ref) e a gerada (ger), como textos de cabeçalho."""
    r, g = ler_face(ref), ler_face(ger)
    res = {"estrutura": [k for k in ("primeiro", "ultimo", "yadv", "rotulos") if r[k] != g[k]],
           "glifos": 0, "desvio": 0, "bits": 0, "caixa": 0, "diferem": []}
    if len(r["glifos"]) != len(g["glifos"]):
        res["estrutura"].append("numero de glifos")
        return res
    for i, (a, b) in enumerate(zip(r["glifos"], g["glifos"])):
        dm = [abs(a[k] - b[k]) for k in CAMPOS]
        ruim = any(dm)
        if ruim:
            res["glifos"] += 1
            res["desvio"] = max(res["desvio"], max(dm))
        if (a["width"], a["height"]) == (b["width"], b["height"]):
            n = sum(x != y for x, y in zip(bits_de(r["bitmap"], a), bits_de(g["bitmap"], b)))
            res["bits"] += n
            ruim = ruim or n > 0
        else:
            res["caixa"] += 1
        if ruim:
            res["diferem"].append(r["primeiro"] + i)
    return res


def problemas_da_face(simbolo, texto):
    ref = (FONTES / (simbolo + ".h")).read_text(encoding="utf-8")
    res = comparar(ref, texto)
    p = [simbolo + ": " + c + " differs" for c in res["estrutura"]]
    medido = (res["glifos"], res["desvio"], res["bits"], res["caixa"], len(res["diferem"]))
    if medido != PISO[simbolo]:
        p.append("%s: measured %s, floor %s (glyphs with another metric, largest deviation, bits, boxes, glyphs "
                 "that differ); a lower number goes into PISO in the same change" % (simbolo, medido, PISO[simbolo]))
    return p


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
    """O versionado com um defeito de forma por vez: cada um tem de mudar o layout."""
    yield "bitmap row without its trailing comma", re.sub(
        r"(  (?:0x[0-9A-F]{2}, ){11}0x[0-9A-F]{2}),\n", r"\1\n", ref, count=1)
    yield "bitmap row of 11 bytes in the middle", re.sub(r"(\n  )0x[0-9A-F]{2}, ", r"\1", ref, count=1)
    yield "PROGMEM misspelled", ref.replace("PROGMEM", "PROGMEN", 1)
    yield "no final newline", ref.rstrip("\n")
    yield "two final newlines", ref + "\n"
    yield "glyph label quoted otherwise", ref.replace("// 0x20 ' '", '// 0x20 " "', 1)
    yield "pragma with two spaces", ref.replace("#pragma once", "#pragma  once", 1)
    yield "blank line between the arrays gone", ref.replace("};\n\nconst GFXglyph", "};\nconst GFXglyph", 1)
    yield "glyph row without spaces", ref.replace("{ 0, 0, 0, 4, 0, 0 }", "{ 0,0,0,4,0,0 }", 1)


# ---------------------------------------------------------------- contexto
def carregar_gen(caminho):
    spec = importlib.util.spec_from_file_location("gen_angulo_face_%x" % abs(hash(str(caminho))), str(caminho))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Ctx:
    def __init__(self, gen_path, ttfdir):
        self.gen_path, self.dir, self.cache = Path(gen_path), Path(ttfdir), {}
        self.tmp = Path(tempfile.mkdtemp(prefix="gen_angulo_face_test."))
        try:
            self.gen, self.erro = carregar_gen(self.gen_path), None
        except Exception as e:  # um gerador que não carrega falha todo controle que o usa
            self.gen, self.erro = None, "%s: %s" % (type(e).__name__, e)

    def ttf(self, simbolo):
        return str(self.dir / FACES[simbolo][0])

    def face(self, simbolo, **kw):
        chave = (simbolo, tuple(sorted(kw.items())))
        if chave not in self.cache:
            self.cache[chave] = self.gen.gerar(self.ttf(simbolo), FACES[simbolo][1], simbolo, **kw)
        return self.cache[chave]

    def run(self, args, env=None):
        e = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        e.update(env or {})
        return subprocess.run([sys.executable, str(self.gen_path)] + [str(a) for a in args],
                              capture_output=True, text=True, encoding="utf-8", env=e, cwd=str(self.tmp))

    def sem_gen(self):
        return ["the generator did not load: %s" % self.erro] if self.gen is None else None

    def versao_certa(self):
        return self.gen is not None and tuple(self.gen.freetype.version()) == FREETYPE


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
    for s in FACES:
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
    if c.sem_gen():
        return c.sem_gen()
    if not c.versao_certa():
        return None
    p = []
    for s in FACES:
        p += problemas_da_face(s, c.face(s)[0])
    return p


def c_negativos(c):
    if c.sem_gen():
        return c.sem_gen()
    if not c.versao_certa():
        return None
    p = []
    base = c.face("SANS9")[0]
    perturbacoes = {"dpi + 9": {"dpi": c.gen.DPI + 9}, "alpha threshold + 40": {"limiar": c.gen.ALFA_MIN + 40}}
    dados = json.loads(EXTRA.read_text(encoding="utf-8"))
    e = dados["entries"]
    for k in ("char", "unicode"):
        e[0][k], e[1][k] = e[1][k], e[0][k]
    (c.tmp / "extra_trocada.json").write_text(json.dumps(dados), encoding="utf-8")
    perturbacoes["two EXTRA entries swapped"] = {"extra": str(c.tmp / "extra_trocada.json")}
    for nome, kw in perturbacoes.items():
        t = c.face("SANS9", **kw)[0]
        if glifos_e_bits(t) == glifos_e_bits(base):
            p.append("%s: the glyphs did not change" % nome)
        elif not problemas_da_face("SANS9", t):
            p.append("%s: the output changed and the floor did not notice" % nome)
    return p


def c_kcapx(c):
    if c.sem_gen():
        return c.sem_gen()
    if not c.versao_certa():
        return None
    k = {s: (int(a), int(b)) for a, b, s in re.findall(
        r"\{\s*(\d+),\s*(\d+)\s*\},\s*// (SANS9|SANSB9|MONO9|MONO12)\b", META.read_text(encoding="utf-8"))}
    p = []
    for s in FACES:
        info = c.face(s)[1]
        medido = (info["cap"], info["xh"], info["digito"])
        linha3 = tuple(int(x) for x in re.search(
            r"Caixa alta (\d+) px, altura de x (\d+) px, digito (\d+) px",
            (FONTES / (s + ".h")).read_text(encoding="utf-8")).groups())
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
    base = [c.ttf("SANSB9"), "9", "SANSB9", str(d / "base.h")]
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


def c_provenance(c):
    if c.sem_gen():
        return c.sem_gen()
    p, d = [], c.tmp / "v"
    d.mkdir()
    base = [c.ttf("SANSB9"), "9", "SANSB9", str(d / "x.h")]
    texto = "FreeSansBold.ttf 20120503 sha256 982534a3, GNU FreeFont (GPL-3.0-or-later with the font exception)"
    if c.run(base).returncode != 0:
        return ["no option: the run failed"]
    sem = (d / "x.h").read_text(encoding="utf-8")
    if not sem.startswith("// SANSB9: FreeSansBold 9 pt a ") or "gerar_fontes_gfx.js" not in sem.split("#pragma")[0]:
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
        if not linhas[1].startswith("// SANSB9: FreeSansBold 9 pt") or "gen_angulo_face.py" not in cab or "gerar_fontes_gfx.js" in cab:
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
    args = [c.ttf("MONO9"), "9", "MONO9", str(d / "a.h")]
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
    ref = (FONTES / "SANS9.h").read_text(encoding="utf-8")
    p = []
    a, b = layout(ref), layout(c.face("SANS9")[0])
    if a != b:
        i = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
        p.append("the generated SANS9 differs in form from the delivered one at token %d: %r against %r"
                 % (i, a[i:i + 1], b[i:i + 1]))
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
    base = [c.ttf("SANS9"), "9", "SANS9", str(saida)]
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


CONTROLES = [("extra", c_extra), ("faces", c_faces), ("negativos", c_negativos), ("kcapx", c_kcapx),
             ("yadv", c_yadv), ("provenance", c_provenance), ("determinismo", c_determinismo),
             ("formato", c_formato), ("ausente", c_ausente)]


def rodar_controles(gen_path, ttfdir):
    """{controle: None (SKIP) ou a lista de problemas}."""
    c = Ctx(gen_path, ttfdir)
    res = {}
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
    ("dpi 141 -> 140", "DPI = 141", "DPI = 140", {"faces"}),
    ("the dpi parameter is ignored", "face.set_char_size(pt << 6, 0, dpi, 0)", "face.set_char_size(pt << 6, 0, DPI, 0)", {"negativos"}),
    ("the threshold parameter is ignored", ">= limiar}", ">= ALFA_MIN}", {"negativos"}),
    ("alpha threshold 128 -> 129", "ALFA_MIN = 128", "ALFA_MIN = 129", {"faces"}),
    ("no gamma table: the coverage is the alpha", "y = x * 12.92 if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055", "y = x", {"faces"}),
    ("no hinting", "FLAGS = freetype.FT_LOAD_TARGET_LIGHT | freetype.FT_LOAD_FORCE_AUTOHINT | freetype.FT_LOAD_NO_BITMAP",
     "FLAGS = freetype.FT_LOAD_NO_HINTING | freetype.FT_LOAD_NO_BITMAP", {"faces", "kcapx"}),
    ("xAdvance truncated, not rounded", "math.floor(g.linearHoriAdvance / 65536.0 + 0.5)", "int(g.linearHoriAdvance / 65536.0)", {"faces"}),
    ("bits packed LSB first", "b = (b << 1) | (bits[i + k] if i + k < len(bits) else 0)",
     "b = (b >> 1) | ((bits[i + k] if i + k < len(bits) else 0) << 7)", {"faces"}),
    ("--y-advance ignored", "y_adv = padrao if y_advance is None else y_advance", "y_adv = padrao", {"yadv"}),
    ("the provenance line is blank", "primeira = None if provenance is None else comment_line(provenance)",
     "primeira = None if provenance is None else ''", {"provenance"}),
    ("FREETYPE_PROPERTIES is not cleared", "os.environ.pop('FREETYPE_PROPERTIES', None)", "pass", {"determinismo"}),
    ("a missing glyph is not refused", "if faltam:", "if False and faltam:", {"ausente"}),
    ("12 bytes a row -> 16", "LINHA = 12", "LINHA = 16", {"formato"}),
    ("pragma with two spaces", "'#pragma once',", "'#pragma  once',", {"formato"}),
    ("the cap height is measured on the 'x'", "cap, xh, dig = (por[c]['h'] for c in 'Hx0')", "cap, xh, dig = (por[c]['h'] for c in 'xx0')", {"kcapx"}),
]


def mutantes(args):
    src = GEN.read_text(encoding="utf-8")
    certo = True
    for nome, ancora, troca, esperado in MUTANTES:
        if src.count(ancora) != 1:
            print("INVALID %s: the anchor occurs %d times in the generator" % (nome, src.count(ancora)))
            certo = False
            continue
        with tempfile.TemporaryDirectory(prefix="gen_angulo_face_mut.") as d:
            raiz = Path(d)
            (raiz / "fonts").mkdir()
            shutil.copy(TOOLS / "subset_font.py", raiz / "subset_font.py")
            shutil.copy(EXTRA, raiz / "fonts" / "angulo_extra.json")
            (raiz / "fonts" / "gen_angulo_face.py").write_text(src.replace(ancora, troca), encoding="utf-8")
            res = rodar_controles(raiz / "fonts" / "gen_angulo_face.py", args.freefont)
        caiu = {n for n, v in res.items() if v}
        bom = caiu == esperado
        certo = certo and bom
        print("%s %s: fails %s (expected %s)" % ("ok  " if bom else "FAIL", nome, sorted(caiu), sorted(esperado)))
    print("%d mutants, %s" % (len(MUTANTES), "all caught by exactly the expected controls" if certo else "SOME NOT CAUGHT AS EXPECTED"))
    return 0 if certo else 1


def medir(args):
    c = Ctx(args.gen, args.freefont)
    if c.gen is None:
        print("the generator did not load:", c.erro)
        return 1
    print("FreeType", tuple(c.gen.freetype.version()))
    for s in FACES:
        texto, info = c.face(s)
        res = comparar((FONTES / (s + ".h")).read_text(encoding="utf-8"), texto)
        print("%s: PISO = %s; cap/x/digit %s; structure %s; differ at %s" % (
            s, (res["glifos"], res["desvio"], res["bits"], res["caixa"], len(res["diferem"])),
            (info["cap"], info["xh"], info["digito"]), res["estrutura"] or "equal", [hex(x) for x in res["diferem"]]))
    shutil.rmtree(c.tmp, ignore_errors=True)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--freefont", required=True, help="pasta com FreeSans.ttf, FreeSansBold.ttf e FreeMonoBold.ttf")
    ap.add_argument("--gen", default=str(GEN), help="o gerador a testar (um mutante, por exemplo)")
    ap.add_argument("--mutants", action="store_true")
    ap.add_argument("--measure", action="store_true")
    args = ap.parse_args()
    for nome, sha in SHA256.items():
        try:
            got = hashlib.sha256((Path(args.freefont) / nome).read_bytes()).hexdigest()
        except OSError as e:
            print("cannot run: %s" % e, file=sys.stderr)
            return 2
        if got != sha:
            print("cannot run: %s has sha256 %s, the pin says %s" % (nome, got, sha), file=sys.stderr)
            return 2
    if args.measure:
        return medir(args)
    if args.mutants:
        return mutantes(args)
    res = rodar_controles(args.gen, args.freefont)
    ruim = 0
    for nome, _ in CONTROLES:
        v = res[nome]
        if v is None:
            print("SKIP %s: the floor was measured with FreeType %s" % (nome, ".".join(map(str, FREETYPE))))
        elif v:
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
