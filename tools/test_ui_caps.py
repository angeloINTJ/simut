#!/usr/bin/env python3
"""Testa src/ui/UiCaps.h: o driver de UI do painel touch (a identidade) e o que sai dele (a capacidade),
pelo pre-processador do host e sem compilar firmware nenhum.

Rodar: python3 tools/test_ui_caps.py

O painel touch tem dois drivers de UI, escolhidos na build (`ui = "<id>"` em tools/features.toml): o original,
o padrao, que e o painel de toda release, e o Angulo (-DSIMUT_UI_ANGULO=1), um estudo ainda nao aprovado para
produtos. Como a rede (tools/test_net_caps.py), a IDENTIDADE e um macro por driver e exatamente um vale 1; o
que o driver sabe fazer (SIMUT_UI_CUSTOM_THEMES: o catalogo de temas e os .thm) sai dela num cabecalho puro,
src/ui/UiCaps.h, e nunca vai por -D: o cabecalho recusa quem o passa.

Este teste pede ao `cpp` do host (o mesmo do test_net_caps.py, que tambem acha o compilador por
shutil.which("cpp") e passa -I<raiz> -I<src>) o veredito de cada montagem, pelo UiCaps.h e pelo
simut_config.h, e prende:

  1. as respostas: sem flag O/A/T (ORIGINAL, ANGULO, CUSTOM_THEMES) = 1/0/1, com o Angulo 0/1/0; o Angulo
     com um valor que nao e 0 nem 1, e um macro derivado passado de fora (com qualquer valor, sozinho ou
     junto de uma linha que parece certa), param num #error;
  2. o Angulo sem a TFT (o LCD 16x2, ou sem mostrador) para num #error do simut_config.h, e o original passa
     em qualquer mostrador;
  3. o cabecalho e puro (sem #include e sem codigo), e o unico que define os macros, e o simut_config.h o
     inclui depois dos macros do mostrador, que o cabecalho nao ve, e antes de usar o Angulo;
  4. cada #error morde: sem a linha, exatamente os casos dele falham (a regra da casa para uma checagem
     nova);
  5. nenhuma linha de comando (platformio.ini, profiles.ini, compose( ) de cada perfil) leva um macro
     derivado, e a identidade do Angulo so vai nas imagens que escolhem `ui = "angulo"` no manifesto: o
     original, escolhido por extenso ou por omissao, compoe o mesmo ambiente e nao emite nada de UI;
  6. o gerador: com um manifesto de brinquedo que da unidades aos dois drivers (nenhum do manifesto de
     hoje tem; as do Angulo chegam depois), as unidades de um driver saem das imagens do outro e o
     `include` entra so na dele; e cada recusa do check_manifest( ) para a UI morde, uma a uma.
"""
import copy
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_features as gf  # noqa: E402

CAPS = os.path.join(SRC, "ui", "UiCaps.h")
CONFIG = os.path.join(SRC, "simut_config.h")

ANGULO = "-DSIMUT_UI_ANGULO=1"

# O que cada sonda imprime: O (ORIGINAL) A (ANGULO) T (CUSTOM_THEMES), na ordem.
NAMES = (("O", "SIMUT_UI_ORIGINAL"), ("A", "SIMUT_UI_ANGULO"), ("T", "SIMUT_UI_CUSTOM_THEMES"))
MACROS = "".join(f"#if {name}\nPROBE_{tag} 1\n#else\nPROBE_{tag} 0\n#endif\n" for tag, name in NAMES)
PROBES = {"caps": '#include "ui/UiCaps.h"\n' + MACROS, "config": '#include "simut_config.h"\n' + MACROS}

# Cada #error, por um trecho do texto que so ele tem.
ERR_DERIVED = "never passed"                       # o do UiCaps.h: um macro derivado passado de fora
ERR_COUNT = "exactly one UI driver"                # o do UiCaps.h: um valor que nao e 0 nem 1
ERR_TFT = "SIMUT_UI_ANGULO needs SIMUT_DISPLAY_TFT"   # o do simut_config.h: o Angulo sem a TFT
CAPS_ERRORS = [ERR_DERIVED, ERR_COUNT]

ORIGINAL_ANSWER, ANGULO_ANSWER = "101", "010"

# Os flags que o manifesto passa a cada mostrador, lidos dele em vez de copiados (tools/features.toml
# [display.*]): o simut_config.h so pode recusar o Angulo onde a TFT nao e a do perfil.
_DISPLAYS = gf.load_manifest()["display"]
LCD = list(_DISPLAYS["alpha_paralelo"]["flags"])
HEADLESS = list(_DISPLAYS["nenhum"]["flags"])

# (rotulo, sonda, flags, veredito). O veredito e ("ok", "OAT") ou ("erro", trecho).
CASES = [
    # -- o UiCaps.h sozinho ---------------------------------------------------------------
    ("sem flag: o original e o padrao", "caps", [], ("ok", ORIGINAL_ANSWER)),
    ("Angulo", "caps", [ANGULO], ("ok", ANGULO_ANSWER)),
    ("Angulo sem valor: -DSIMUT_UI_ANGULO vale 1", "caps", ["-DSIMUT_UI_ANGULO"], ("ok", ANGULO_ANSWER)),
    ("Angulo=0 por extenso: o padrao", "caps", ["-DSIMUT_UI_ANGULO=0"], ("ok", ORIGINAL_ANSWER)),
    ("Angulo=2: nao e um driver", "caps", ["-DSIMUT_UI_ANGULO=2"], ("erro", ERR_COUNT)),
    ("Angulo=-1: nao e um driver", "caps", ["-DSIMUT_UI_ANGULO=-1"], ("erro", ERR_COUNT)),
    ("o original passado de fora", "caps", ["-DSIMUT_UI_ORIGINAL=1"], ("erro", ERR_DERIVED)),
    ("o original passado de fora com valor 0: o valor nao importa", "caps", ["-DSIMUT_UI_ORIGINAL=0"],
     ("erro", ERR_DERIVED)),
    ("os temas passados de fora", "caps", ["-DSIMUT_UI_CUSTOM_THEMES=1"], ("erro", ERR_DERIVED)),
    ("os temas passados de fora com valor 0", "caps", ["-DSIMUT_UI_CUSTOM_THEMES=0"], ("erro", ERR_DERIVED)),
    ("a linha que parece certa: o original=0 com o Angulo", "caps", ["-DSIMUT_UI_ORIGINAL=0", ANGULO],
     ("erro", ERR_DERIVED)),
    ("o original=1 junto do Angulo", "caps", ["-DSIMUT_UI_ORIGINAL=1", ANGULO], ("erro", ERR_DERIVED)),
    ("os temas=0 junto do Angulo", "caps", [ANGULO, "-DSIMUT_UI_CUSTOM_THEMES=0"], ("erro", ERR_DERIVED)),
    # -- as mesmas respostas pelo simut_config.h, e o Angulo contra o mostrador -------------
    ("simut_config.h sem flag", "config", [], ("ok", ORIGINAL_ANSWER)),
    ("simut_config.h com o Angulo na TFT", "config", [ANGULO], ("ok", ANGULO_ANSWER)),
    ("simut_config.h com a TFT por extenso e o Angulo", "config", ["-DSIMUT_DISPLAY_TFT=1", ANGULO],
     ("ok", ANGULO_ANSWER)),
    ("simut_config.h: o original no LCD 16x2", "config", LCD, ("ok", ORIGINAL_ANSWER)),
    ("simut_config.h: o original sem mostrador", "config", HEADLESS, ("ok", ORIGINAL_ANSWER)),
    ("simut_config.h: o Angulo no LCD 16x2", "config", LCD + [ANGULO], ("erro", ERR_TFT)),
    ("simut_config.h: o Angulo sem mostrador", "config", HEADLESS + [ANGULO], ("erro", ERR_TFT)),
    ("simut_config.h: o Angulo com a TFT desligada e nenhum mostrador dito", "config",
     ["-DSIMUT_DISPLAY_TFT=0", ANGULO], ("erro", ERR_TFT)),
    ("simut_config.h com o Angulo=2", "config", ["-DSIMUT_UI_ANGULO=2"], ("erro", ERR_COUNT)),
    ("simut_config.h com o original de fora", "config", ["-DSIMUT_UI_ORIGINAL=1"], ("erro", ERR_DERIVED)),
    ("simut_config.h com os temas de fora", "config", ["-DSIMUT_UI_CUSTOM_THEMES=1"], ("erro", ERR_DERIVED)),
]

ERR_LINE = re.compile(r"error: #error")


def preprocess(flags, probe_text, root, tmp):
    """O `cpp` do host sobre a sonda: (codigo de saida, "OAT" lido, linhas de #error, ultima linha do stderr)."""
    probe = os.path.join(tmp, "probe.c")
    with open(probe, "w", encoding="utf-8") as fh:
        fh.write(probe_text)
    r = subprocess.run(["cpp", "-P", f"-I{root}", f"-I{SRC}"] + flags + [probe], capture_output=True, text=True)
    errors = [ln for ln in r.stderr.splitlines() if ERR_LINE.search(ln)]
    seen = {}
    for ln in r.stdout.splitlines():
        m = re.fullmatch(r"PROBE_([OAT]) ([01])", ln.strip())
        if m:
            seen[m.group(1)] = m.group(2)
    answer = "".join(seen.get(tag, "?") for tag, _ in NAMES)
    tail = (r.stderr.strip().splitlines() or [""])[-1]
    return r.returncode, answer, errors, tail


def judge(case, root, tmp):
    """None se o caso passa; senao, o que deu errado."""
    label, probe, flags, (kind, what) = case
    code, answer, errors, tail = preprocess(flags, PROBES[probe], root, tmp)
    if kind == "erro":
        if code == 0 or not errors:
            return (f"era para parar num #error ({what!r}) e foi aceito como {answer}" if code == 0 else
                    f"era para parar num #error ({what!r}) e o cpp falhou por outro motivo: {tail}")
        if len(errors) != 1 or what not in errors[0]:
            return f"parou, mas no #error errado ({what!r} esperado): {errors}"
        return None
    if code != 0 or errors:
        return f"era para ser aceito como {what} e parou: {errors or tail or code}"
    if answer != what:
        return f"aceito, mas ORIGINAL/ANGULO/CUSTOM_THEMES = {answer} em vez de {what}"
    return None


def run_all(cases, root, tmp):
    return [(c[0], judge(c, root, tmp)) for c in cases]


def write_tree(base, name, config_text, caps_text):
    """Uma raiz de includes com o simut_config.h e o ui/UiCaps.h dados; o resto (net/NetCaps.h) o -I<src> acha."""
    root = os.path.join(base, name)
    os.makedirs(os.path.join(root, "ui"), exist_ok=True)
    with open(os.path.join(root, "simut_config.h"), "w", encoding="utf-8") as fh:
        fh.write(config_text)
    with open(os.path.join(root, "ui", "UiCaps.h"), "w", encoding="utf-8") as fh:
        fh.write(caps_text)
    return root


def blank_error(text, token):
    """O texto sem a linha `#error` que tem `token` (a linha vira vazia: as demais nao andam)."""
    lines = text.split("\n")
    hit = [i for i, ln in enumerate(lines) if ln.startswith("#error") and token in ln]
    if len(hit) != 1:
        raise ValueError(f"o #error de {token!r} esta em {len(hit)} linhas, esperava 1")
    lines[hit[0]] = ""
    return "\n".join(lines)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def main():
    if shutil.which("cpp") is None:
        print("FALHOU cpp do host disponivel\n    instale o gcc: o teste pre-processa o cabecalho")
        return 1
    fails = []

    if not os.path.isfile(CAPS):
        print("test_ui_caps: FALHOU")
        print(f"  - {os.path.relpath(CAPS, ROOT)} nao existe: a identidade do driver de UI ainda nao tem cabecalho")
        return 1
    with open(CAPS, encoding="utf-8") as fh:
        caps_text = fh.read()
    with open(CONFIG, encoding="utf-8") as fh:
        config_text = fh.read()

    # 3. O cabecalho e puro e e o unico que define os macros.
    code = strip_comments(caps_text)
    if re.search(r"^\s*#\s*include\b", code, re.M):
        fails.append("o UiCaps.h tem um #include: ele nao pode ter, para ser seguro nas suites do host")
    loose = [ln for ln in code.split("\n") if ln.strip() and not ln.lstrip().startswith("#") and not ln.rstrip().endswith("\\")]
    if loose:
        fails.append(f"o UiCaps.h tem codigo alem de diretivas: {loose[:3]}")
    if "#pragma once" not in code:
        fails.append("o UiCaps.h sem #pragma once: incluido duas vezes ele recusaria os proprios macros")
    definers = {}
    for dirpath, _dirs, files in os.walk(SRC):
        for f in files:
            if f.endswith((".h", ".hpp", ".cpp", ".c", ".ino")):
                path = os.path.join(dirpath, f)
                with open(path, encoding="utf-8", errors="ignore") as fh:
                    for n, ln in enumerate(fh, 1):
                        if re.match(r"\s*#\s*define\s+SIMUT_UI_(ORIGINAL|ANGULO|CUSTOM_THEMES)\b", ln):
                            definers.setdefault(os.path.relpath(path, SRC), []).append(n)
    if sorted(definers) != [os.path.join("ui", "UiCaps.h")] or [len(v) for v in definers.values()] != [3]:
        fails.append("a identidade e a capacidade (SIMUT_UI_ORIGINAL, SIMUT_UI_ANGULO, SIMUT_UI_CUSTOM_THEMES) tem de "
                     f"ser definida so no ui/UiCaps.h, uma vez cada: {definers}")

    # O simut_config.h inclui o cabecalho depois dos macros do mostrador (que o cabecalho nao ve) e antes
    # de usar o Angulo no #error: o inverso testaria um macro ainda indefinido, que vale 0 em silencio.
    cfg_lines = config_text.split("\n")

    def where(pattern):
        return [i for i, ln in enumerate(cfg_lines) if re.match(pattern, ln)]

    at_tft, at_alpha = where(r"#define SIMUT_DISPLAY_TFT\b"), where(r"#define SIMUT_DISPLAY_ALPHA\b")
    at_inc = where(r'#include "ui/UiCaps\.h"')
    at_err = [i for i, ln in enumerate(cfg_lines) if ln.startswith("#error") and ERR_TFT in ln]
    if len(at_tft) != 1 or len(at_alpha) != 1 or len(at_inc) != 1 or len(at_err) != 1:
        fails.append("o simut_config.h tem de definir o mostrador (TFT e ALPHA) uma vez, incluir o ui/UiCaps.h uma "
                     f"vez e ter um #error do Angulo so: {len(at_tft)}, {len(at_alpha)}, {len(at_inc)}, {len(at_err)}")
    elif not max(at_tft[0], at_alpha[0]) < at_inc[0] < at_err[0]:
        fails.append("no simut_config.h a ordem tem de ser: os macros do mostrador, o #include do ui/UiCaps.h, o #error "
                     f"do Angulo; esta nas linhas {at_tft[0] + 1}/{at_alpha[0] + 1}, {at_inc[0] + 1}, {at_err[0] + 1}")

    tmp = tempfile.mkdtemp(prefix="ui_caps_")
    bitten = 0
    try:
        # 1 e 2. As respostas, pelo UiCaps.h e pelo simut_config.h de verdade.
        baseline_ok = True
        for label, why in run_all(CASES, SRC, tmp):
            if why:
                baseline_ok = False
                fails.append(f"{label}: {why}")

        # 4. Cada #error morde. Os do UiCaps.h saem do UiCaps.h; o do simut_config.h sai do simut_config.h.
        error_lines = [ln for ln in caps_text.split("\n") if ln.startswith("#error")]
        for token in CAPS_ERRORS:
            if sum(token in ln for ln in error_lines) != 1:
                fails.append(f"o #error de {token!r} esta em {sum(token in ln for ln in error_lines)} linhas do UiCaps.h, esperava 1")
        stranger = [ln for ln in error_lines if not any(t in ln for t in CAPS_ERRORS)]
        if stranger:
            fails.append(f"#error do UiCaps.h sem caso: {stranger}")
        all_errors = CAPS_ERRORS + [ERR_TFT]
        for token in all_errors:
            if not [c for c in CASES if c[3] == ("erro", token)]:
                fails.append(f"o #error de {token!r} nao tem caso que o provoque")
        if baseline_ok:
            for n, token in enumerate(all_errors):
                try:
                    if token == ERR_TFT:
                        mutant = write_tree(tmp, f"m{n}", blank_error(config_text, token), caps_text)
                    else:
                        mutant = write_tree(tmp, f"m{n}", config_text, blank_error(caps_text, token))
                except ValueError as e:
                    fails.append(str(e))
                    continue
                failed = [label for label, why in run_all(CASES, mutant, tmp) if why]
                want = [c[0] for c in CASES if c[3] == ("erro", token)]
                if sorted(failed) == sorted(want):
                    bitten += 1
                else:
                    fails.append(f"sem o #error de {token!r}, falharam {failed or 'nenhum caso'}; era para falhar so {want}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # 5. O manifesto. O macro derivado nunca vai por -D (o UiCaps.h o recusa); a identidade do Angulo vai nos
    #    flags do driver e so nas imagens que o escolhem; o original, o padrao, nao emite nada.
    M = gf.load_manifest()
    derived = re.compile(r"-DSIMUT_UI_(ORIGINAL|CUSTOM_THEMES)\b")
    for path in ("platformio.ini", os.path.join("tools", "generated", "profiles.ini")):
        with open(os.path.join(ROOT, path), encoding="utf-8") as fh:
            hit = [ln.strip() for ln in fh if derived.search(ln)]
        if hit:
            fails.append(f"{path} passa um macro derivado da UI por -D (o UiCaps.h o recusa): {hit}")
    if M["ui"]["angulo"].get("flags") != [ANGULO]:
        fails.append(f"[ui.angulo].flags e {M['ui']['angulo'].get('flags')}, a identidade que o UiCaps.h le e {ANGULO}")
    if M["ui"][gf.DEFAULT_UI].get("flags"):
        fails.append(f"[ui.{gf.DEFAULT_UI}] e o padrao e nao pode emitir flag: {M['ui'][gf.DEFAULT_UI]['flags']}")
    ui_flag = re.compile(r"-DSIMUT_UI_")
    angulo_images = 0
    for name in M["profiles"]:
        prof = gf.resolve_profile(name, M["profiles"])
        flags = gf.compose(prof, M)["flags"]
        if [f for f in flags if derived.match(f)]:
            fails.append(f"compose( ) de {name} passa um macro derivado da UI por -D: {[f for f in flags if derived.match(f)]}")
        want = [ANGULO] if prof.get("ui", gf.DEFAULT_UI) == "angulo" else []
        angulo_images += bool(want)
        got = [f for f in flags if ui_flag.match(f)]
        if got != want:
            fails.append(f"compose( ) de {name} leva os flags de UI {got}, e o driver {prof.get('ui', gf.DEFAULT_UI)!r} manda {want}")
        # O original por extenso e o original por omissao sao a mesma imagem.
        bare = {k: v for k, v in prof.items() if k != "ui"}
        if prof.get("ui", gf.DEFAULT_UI) == gf.DEFAULT_UI and gf.compose(dict(prof, ui=gf.DEFAULT_UI), M) != gf.compose(bare, M):
            fails.append(f"{name}: ui = \"{gf.DEFAULT_UI}\" por extenso compoe outro ambiente que omitir a chave")
    if not angulo_images:
        fails.append("nenhum perfil do manifesto escolhe o Angulo: o caso acima nao provou nada")
    # O que a pagina le (docs/configurador/model.json) diz o mesmo driver de cada produto que o gerador.
    with open(os.path.join(ROOT, "docs", "configurador", "model.json"), encoding="utf-8") as fh:
        model = json.load(fh)
    for p in model["products"]:
        want = gf.config_of(gf.resolve_profile(p["id"], M["profiles"]))["ui"]
        if p["config"].get("ui") != want:
            fails.append(f"model.json: {p['id']} tem config.ui {p['config'].get('ui')!r}, o manifesto diz {want!r}")
        # E o chip, que as regras leem desde 2026-10-22 (a do Angulo no Pico W): o do produto, como o manifesto o diz.
        want = gf.resolve_profile(p["id"], M["profiles"]).get("chip") or "rp2040"
        if p["config"].get("chip") != want:
            fails.append(f"model.json: {p['id']} tem config.chip {p['config'].get('chip')!r}, o manifesto diz {want!r}")
    if [o["id"] for o in model["ui"]["options"]] != sorted(M["ui"], key=lambda n: M["ui_panels"][n]["order"]):
        fails.append(f"model.json: as opcoes de UI {[o['id'] for o in model['ui']['options']]} nao seguem o order de [ui_panels]")

    # O que as regras do Angulo e a ajuda dele dizem (S10c C1a-b, 2026-10-22). A revisao do C1a achou tres frases que nao eram verdade: a recusa do Pico W dizia que o Angulo
    # "nao cabe" (medido so na imagem de release; a regra recusa todo perfil do Pico W pela decisao do mantenedor de 2026-10-21), a ajuda dizia que o custo em flash "nao foi
    # medido" (a evidencia da regra o cita), e faltava a regra do grafico do painel. Cada frase abaixo e a de uma lingua que a pagina ou os FEATURES*.md mostram.
    rules = {r["id"]: r for r in M["rules"]}
    pico_w, grafico = rules.get("angulo-cyw43-needs-rp2350"), rules.get("angulo-needs-tft-graph")
    if pico_w is None or grafico is None:
        fails.append(f"faltam regras do Angulo no manifesto: angulo-cyw43-needs-rp2350 {pico_w is not None}, angulo-needs-tft-graph {grafico is not None}")
    else:
        for lang, diz, nao_diz in (("en", ("not offered yet", "Choose the Pico 2 W", "original look"), r"\bfit"),
                                   ("pt", ("ainda não é oferecido", "Escolha o Pico 2 W", "visual original"), r"\bcabe"),
                                   ("es", ("aún no se ofrece", "Elija la Pico 2 W", "aspecto original"), r"\bcabe")):
            texto = pico_w["why"][lang]
            for tk in diz:
                if tk not in texto:
                    fails.append(f"angulo-cyw43-needs-rp2350.why.{lang} não diz «{tk}»: {texto!r}")
            if re.search(nao_diz, texto):
                fails.append(f"angulo-cyw43-needs-rp2350.why.{lang} diz que o Angulo não cabe, e isso foi medido só na release do Pico W: {texto!r}")
        for tk in ("decided on 2026-10-21", "every Pico W profile", "45,856 B", "31,008 B"):
            if tk not in pico_w["evidence"]:
                fails.append(f"angulo-cyw43-needs-rp2350.evidence não diz «{tk}» (a decisão com a data, e a medida que ficou)")
        for lang in ("en", "pt", "es"):
            rotulo = M["ui_toggles"]["tft_graph"]["label"][lang]
            if rotulo not in grafico["why"][lang]:
                fails.append(f"angulo-needs-tft-graph.why.{lang} não chama a chave pelo rótulo que a página mostra, «{rotulo}»: {grafico['why'][lang]!r}")
        for tk in ("2026-10-22", "python3 tools/build_custom.py --profile", "\"tft_graph\":false", "'_graphData' was not declared in this scope", "DisplayManager_UiA.cpp:293:44"):
            if tk not in grafico["evidence"]:
                fails.append(f"angulo-needs-tft-graph.evidence não cita «{tk}» (o erro do compilador, o comando e a data)")
        figura = re.search(r"is ([\d,]+) B above pico2_w_release", pico_w["evidence"])
        for lang, vazio, fig in (("en", "has not been measured", figura and figura.group(1)),
                                 ("pt", "ainda não foi medido", figura and figura.group(1).replace(",", ".")),
                                 ("es", "aún no se midió", figura and figura.group(1).replace(",", "."))):
            ajuda = M["ui_panels"]["angulo"]["help"][lang]
            if vazio in ajuda:
                fails.append(f"ui_panels.angulo.help.{lang} diz que o custo em flash «{vazio}», e a regra do Pico W cita a medida dele: {ajuda!r}")
            if not fig or f"{fig} B" not in ajuda:
                fails.append(f"ui_panels.angulo.help.{lang} não traz a medida que a evidência da regra do Pico W dá ({fig} B): {ajuda!r}")

    # 6. O gerador, com manifestos de brinquedo. As unidades de um driver de UI saem das imagens dos outros e o
    #    `include` entra so na dele, como as de um driver de rede. Nenhum driver do manifesto de hoje tem unidades
    #    (as do Angulo chegam em S2a), entao sem este brinquedo o laco de compose( ) nao teria quem o exercitasse.
    toy = copy.deepcopy(M)
    toy["ui"]["original"]["units"] = ["DisplayManager_Touch.cpp"]
    toy["ui"]["angulo"]["include"] = ["ui/angulo/*.cpp"]
    toy["ui"]["angulo"]["units"] = ["ui/angulo/Toy.cpp"]
    base = gf.resolve_profile("pico_wiretft", toy["profiles"])
    orig, ang = gf.compose(dict(base, ui="original"), toy), gf.compose(dict(base, ui="angulo"), toy)
    for what, ok in (
            ("as unidades do original saem da imagem do Angulo", "DisplayManager_Touch.cpp" in ang["excludes"]),
            ("e ficam na do original", "DisplayManager_Touch.cpp" not in orig["excludes"]),
            ("as do Angulo saem da imagem do original", "ui/angulo/Toy.cpp" in orig["excludes"]),
            ("e ficam na do Angulo", "ui/angulo/Toy.cpp" not in ang["excludes"]),
            ("o include do Angulo entra na imagem dele", "ui/angulo/*.cpp" in ang["includes"]),
            ("e nao na do original", "ui/angulo/*.cpp" not in orig["includes"])):
        if not ok:
            fails.append(f"compose( ) com unidades de brinquedo: {what} falhou")
    try:
        gf.check_manifest(toy)
    except SystemExit as e:
        fails.append(f"check_manifest recusou o manifesto de brinquedo, que e valido: {e.code}")
    for bad in ("neon", ["angulo"], 1, None):
        try:
            gf.compose(dict(base, ui=bad), M)
        except SystemExit as e:
            if "nao e um [ui.*]" not in str(e.code):
                fails.append(f"compose( ) com ui = {bad!r} recusou por outro motivo: {e.code}")
        else:
            fails.append(f"compose( ) aceitou ui = {bad!r}, que nao e um driver do manifesto")
    n_refusals = 0

    def rule(t):
        return next(r for r in t["rules"] if r["id"] == "angulo-needs-tft")

    def rule_chip(t):   # the Angulo on the Pico W (2026-10-22): `chip` is in the rules' vocabulary since then
        return next(r for r in t["rules"] if r["id"] == "angulo-cyw43-needs-rp2350")

    # (rotulo, como estragar o manifesto, trecho da recusa). Cada uma tem de parar o check_manifest( ).
    REFUSALS = [
        ("sem o driver padrao", lambda t: t["ui"].pop(gf.DEFAULT_UI), f"falta [ui.{gf.DEFAULT_UI}]"),
        ("chave que ninguem le", lambda t: t["ui"]["angulo"].update(colour=1), "tem chave que ninguem le"),
        ("flag sem -D", lambda t: t["ui"]["angulo"].update(flags=["SIMUT_UI_ANGULO=1"]), "[ui.angulo].flags tem de ser"),
        ("flags que nao e lista", lambda t: t["ui"]["angulo"].update(flags="-DSIMUT_UI_ANGULO=1"), "[ui.angulo].flags tem de ser"),
        ("unidade com espaco", lambda t: t["ui"]["angulo"].update(units=["a b.cpp"]), "[ui.angulo].units tem de ser"),
        ("include que nao e lista", lambda t: t["ui"]["angulo"].update(include="x.cpp"), "[ui.angulo].include tem de ser"),
        ("driver sem as palavras da pagina", lambda t: t["ui_panels"].pop("angulo"), "sem [ui_panels.angulo]"),
        ("palavras sem driver", lambda t: t["ui_panels"].update(ghost=dict(t["ui_panels"]["angulo"])), "sem [ui.ghost]"),
        ("palavras sem a ajuda", lambda t: t["ui_panels"]["angulo"].pop("help"), "ui_panels.angulo.help"),
        ("opcao sem order inteiro", lambda t: t["ui_panels"]["angulo"].update(order="2"), "precisa de um order inteiro"),
        ("sem a escolha da pagina", lambda t: t.pop("ui_panel"), "falta [ui_panel]"),
        ("grupo da escolha que nao existe", lambda t: t["ui_panel"].update(group="nowhere"), "nao e um [ui_groups]"),
        ("perfil com uma UI que nao existe", lambda t: t["profiles"]["pico_wiretft_angulo"].update(ui="neon"), "nao e um [ui.*]"),
        ("regra sobre uma UI que nao existe", lambda t: rule(t)["when"].update(ui="neon"), "UI desconhecida"),
        ("regra que exige um chip que nao existe", lambda t: rule_chip(t)["require"].update(chip="rp9999"), "chip desconhecido"),
        ("regra que exige um chip que nao e texto", lambda t: rule_chip(t)["require"].update(chip=True), "chip desconhecido"),
        ("regra sobre uma rede que nao existe, ao lado do chip", lambda t: rule_chip(t)["when"].update(net="w5500"), "rede desconhecida"),
    ]
    for label, mutate, token in REFUSALS:
        bad = copy.deepcopy(M)
        mutate(bad)
        try:
            gf.check_manifest(bad)
        except SystemExit as e:
            if token in str(e.code):
                n_refusals += 1
            else:
                fails.append(f"check_manifest recusou «{label}» por outro motivo ({token!r} esperado): {e.code}")
        else:
            fails.append(f"check_manifest aceitou «{label}»")

    if fails:
        print("test_ui_caps: FALHOU")
        for f in fails:
            print("  -", f)
        return 1
    print(f"test_ui_caps: {len(CASES)} montagens (pelo UiCaps.h e pelo simut_config.h) julgadas pelo pre-processador; "
          f"{bitten} de {len(all_errors)} #error mordem (sem a linha, exatamente os casos dele falham); "
          f"{len(M['profiles'])} imagens do manifesto sem macro derivado por -D ({angulo_images} com a identidade do Angulo); "
          f"o gerador compoe as unidades de dois drivers de brinquedo e recusa {n_refusals} de {len(REFUSALS)} manifestos estragados.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
