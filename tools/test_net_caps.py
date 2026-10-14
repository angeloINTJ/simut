#!/usr/bin/env python3
"""Testa src/net/NetCaps.h: a identidade da rede (o driver, o chip) separada da classe dela
(Wi-Fi ou cabo), pelo pre-processador do host e sem compilar firmware nenhum.

Rodar: python3 tools/test_net_caps.py

O firmware tem dois drivers de rede: o radio CYW43 do Pico W (o padrao) e o modulo ENC28J60 no
cabo (`net = "enc28j60"` em tools/features.toml). Ate 2026-10-13 o macro SIMUT_NET_LAN dizia duas
coisas ao mesmo tempo: "o enlace e um cabo" (a CLASSE: esconder o SSID, mostrar Cabo) e "o chip e
o ENC28J60" (a IDENTIDADE: os pinos GP11-GP15, o programa PIO, a biblioteca, os onze slots). Um
segundo modulo cabeado herdaria os pinos do ENC28J60. Agora:

  - a IDENTIDADE e um macro por driver, exatamente um vale 1: SIMUT_NET_CYW43 (o padrao, quando
    nenhum e dito) e SIMUT_NET_ENC28J60. O manifesto a passa por -D;
  - a CLASSE (SIMUT_NET_WIFI = CYW43, SIMUT_NET_LAN = ENC28J60) sai da identidade, num cabecalho
    puro so, src/net/NetCaps.h, e nunca vai por -D: o cabecalho recusa quem a passa.

Este teste pede ao `cpp` do host (o mesmo do test_pin_masks.py) o veredito de cada montagem, pelo
NetCaps.h e pelo simut_config.h, e prende:

  1. as respostas: sem flag 1/0/1/0 (WIFI/LAN/CYW43/ENC28J60), com o ENC28J60 0/1/0/1; os dois
     drivers, nenhum, e a classe passada de fora (LAN ou WIFI, com qualquer valor) param num #error;
  2. o cabecalho e puro (sem #include e sem codigo: so e seguro nas suites do host e em quem nao
     inclui nada) e e o unico lugar que define a classe;
  3. a receita do comentario do NetCaps.h para um segundo driver cabeado (aqui, um W5500 de
     brinquedo) funciona: o padrao segue o radio, a conta recusa dois drivers, e o simut_config.h
     recusa o nome de OTA que ele ainda nao tem;
  3b. o ultimo passo da receita, o cabecalho de dispositivo do chip (src/net/<Chip>Device.h) e o
     `#elif` dele na cadeia de includes do src/net/WiredLink.cpp: o WiredLink.cpp de verdade,
     pre-processado, para num #error para um driver cabeado que nao tem o cabecalho, aceita o
     ENC28J60 e o brinquedo que o tem, e nao expande nada numa imagem de radio;
  4. cada #error morde: sem a linha, exatamente os casos dele falham (a regra da casa para uma
     checagem nova);
  5. nenhuma linha de comando (platformio.ini, profiles.ini, compose( ) de cada perfil) leva a
     classe, e o leitor de flags do PlatformIO (tools/check_cli_help.py, que roda em todo `pio run`)
     acha a classe pela identidade: sem isso ele avaliaria a imagem cabeada como se fosse a do radio,
     em silencio.
"""
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

CAPS = os.path.join(SRC, "net", "NetCaps.h")
CONFIG = os.path.join(SRC, "simut_config.h")

ENC = "-DSIMUT_NET_ENC28J60=1"
SLOTS = "-DMAX_SENSORS=11"       # os onze slots: sem isso o simut_config.h recusa o modulo nos slots

# O que cada sonda imprime: W (WIFI) L (LAN) C (CYW43) E (ENC28J60), na ordem.
NAMES = (("W", "SIMUT_NET_WIFI"), ("L", "SIMUT_NET_LAN"), ("C", "SIMUT_NET_CYW43"), ("E", "SIMUT_NET_ENC28J60"))
MACROS = "".join(f"#if {name}\nPROBE_{tag} 1\n#else\nPROBE_{tag} 0\n#endif\n" for tag, name in NAMES)
PROBES = {"caps": '#include "net/NetCaps.h"\n' + MACROS, "config": '#include "simut_config.h"\n' + MACROS,
          "wired": '#include "net/WiredLink.cpp"\n'}

# Cada #error, por um trecho do texto que so ele tem.
ERR_CLASS = "never passed"
ERR_COUNT = "exactly one network driver"
ERR_OTA = "its own OTA variant names"          # o do simut_config.h
ERR_DEVICE = "needs its device header"         # o do src/net/WiredLink.cpp
CAPS_ERRORS = [ERR_CLASS, ERR_COUNT]
WIRED_CPP = os.path.join(SRC, "net", "WiredLink.cpp")
# O que o src/net/WiredLink.cpp tem de expandir, ou de nao expandir, para cada montagem: o namespace da API, e
# o que so o cabecalho de dispositivo do chip traz (a biblioteca do ENC28J60; o marcador do brinquedo).
WIRED_API = "namespace WiredLink"
WIRED_ENC = ((WIRED_API, "PicoENC28J60lwIP"), ())
WIRED_TOY = ((WIRED_API, "TOY_W5500_DEVICE"), ())
WIRED_NOTHING = ((), (WIRED_API,))

RADIO, CABLE = "1010", "0101"

# (rotulo, sonda, arvore, flags, veredito). A arvore e "real" (o src/) ou "segundo" (o src/ com um
# segundo driver cabeado, feito pela receita do comentario). O veredito e ("ok", "WLCE") ou ("erro", trecho).
CASES = [
    # -- o NetCaps.h sozinho --------------------------------------------------------------
    ("sem flag: o CYW43 e o padrao", "caps", "real", [], ("ok", RADIO)),
    ("ENC28J60: a classe cabo", "caps", "real", [ENC], ("ok", CABLE)),
    ("CYW43 dito por extenso", "caps", "real", ["-DSIMUT_NET_CYW43=1"], ("ok", RADIO)),
    ("ENC28J60=0 por extenso: o padrao", "caps", "real", ["-DSIMUT_NET_ENC28J60=0"], ("ok", RADIO)),
    ("ENC28J60 com o CYW43=0 por extenso", "caps", "real", [ENC, "-DSIMUT_NET_CYW43=0"], ("ok", CABLE)),
    ("os dois drivers", "caps", "real", [ENC, "-DSIMUT_NET_CYW43=1"], ("erro", ERR_COUNT)),
    ("nenhum driver", "caps", "real", ["-DSIMUT_NET_CYW43=0"], ("erro", ERR_COUNT)),
    ("a classe LAN passada de fora", "caps", "real", ["-DSIMUT_NET_LAN=1"], ("erro", ERR_CLASS)),
    ("a classe WIFI passada de fora", "caps", "real", ["-DSIMUT_NET_WIFI=1"], ("erro", ERR_CLASS)),
    ("a classe LAN passada de fora com valor 0: o valor nao importa", "caps", "real", ["-DSIMUT_NET_LAN=0"], ("erro", ERR_CLASS)),
    ("a classe WIFI passada de fora com valor 0", "caps", "real", ["-DSIMUT_NET_WIFI=0"], ("erro", ERR_CLASS)),
    ("as duas classes de fora", "caps", "real", ["-DSIMUT_NET_LAN=1", "-DSIMUT_NET_WIFI=0"], ("erro", ERR_CLASS)),
    ("a linha de comando de antes: a classe LAN com o ENC28J60", "caps", "real", ["-DSIMUT_NET_LAN=1", ENC], ("erro", ERR_CLASS)),
    # -- as mesmas respostas pelo simut_config.h ------------------------------------------
    ("simut_config.h sem flag", "config", "real", [], ("ok", RADIO)),
    ("simut_config.h com o ENC28J60", "config", "real", [ENC, SLOTS], ("ok", CABLE)),
    ("simut_config.h com os dois drivers", "config", "real", [ENC, SLOTS, "-DSIMUT_NET_CYW43=1"], ("erro", ERR_COUNT)),
    ("simut_config.h sem driver nenhum", "config", "real", ["-DSIMUT_NET_CYW43=0"], ("erro", ERR_COUNT)),
    ("simut_config.h com a classe LAN de fora", "config", "real", ["-DSIMUT_NET_LAN=1"], ("erro", ERR_CLASS)),
    ("simut_config.h com a classe WIFI de fora", "config", "real", ["-DSIMUT_NET_WIFI=1"], ("erro", ERR_CLASS)),
    # -- o segundo driver cabeado, pela receita do comentario ------------------------------
    ("segundo driver: sem flag o padrao segue o radio", "caps", "segundo", [], ("ok", RADIO)),
    ("segundo driver sozinho: a classe cabo, e nao e o ENC28J60", "caps", "segundo", ["-DSIMUT_NET_W5500=1"], ("ok", "0100")),
    ("segundo driver com o ENC28J60", "caps", "segundo", ["-DSIMUT_NET_W5500=1", ENC], ("erro", ERR_COUNT)),
    ("segundo driver com o CYW43 dito por extenso", "caps", "segundo", ["-DSIMUT_NET_W5500=1", "-DSIMUT_NET_CYW43=1"], ("erro", ERR_COUNT)),
    ("segundo driver: o ENC28J60 segue sendo a classe cabo", "caps", "segundo", [ENC], ("ok", CABLE)),
    ("simut_config.h: o ENC28J60 segue com o nome de sempre", "config", "segundo", [ENC, SLOTS], ("ok", CABLE)),
    ("simut_config.h: um segundo driver cabeado ainda nao tem nome de OTA", "config", "segundo", ["-DSIMUT_NET_W5500=1"], ("erro", ERR_OTA)),
    # -- o ultimo passo da receita: o cabecalho de dispositivo e o #elif no WiredLink.cpp ----------------
    ("WiredLink.cpp: o ENC28J60 tem o cabecalho de dispositivo", "wired", "cabo", [ENC], ("ok", WIRED_ENC)),
    ("WiredLink.cpp: uma imagem de radio nao expande nada", "wired", "cabo", [], ("ok", WIRED_NOTHING)),
    ("WiredLink.cpp: um segundo driver cabeado sem cabecalho de dispositivo", "wired", "segundo_cabo", ["-DSIMUT_NET_W5500=1"],
     ("erro", ERR_DEVICE)),
    ("WiredLink.cpp: o segundo driver com o cabecalho e o #elif da receita", "wired", "segundo_cabo_ok", ["-DSIMUT_NET_W5500=1"],
     ("ok", WIRED_TOY)),
    ("WiredLink.cpp: o ENC28J60 segue com o dele ao lado do #elif do segundo", "wired", "segundo_cabo_ok", [ENC], ("ok", WIRED_ENC)),
]

ERR_LINE = re.compile(r"error: #error")


def preprocess(flags, probe_text, root, tmp):
    """O `cpp` do host sobre a sonda: (codigo de saida, "WLCE" lido, linhas de #error, ultima linha do stderr, expansao)."""
    probe = os.path.join(tmp, "probe.c")
    with open(probe, "w", encoding="utf-8") as fh:
        fh.write(probe_text)
    r = subprocess.run(["cpp", "-P", f"-I{root}", f"-I{SRC}"] + flags + [probe], capture_output=True, text=True)
    errors = [ln for ln in r.stderr.splitlines() if ERR_LINE.search(ln)]
    seen = {}
    for ln in r.stdout.splitlines():
        m = re.fullmatch(r"PROBE_([WLCE]) ([01])", ln.strip())
        if m:
            seen[m.group(1)] = m.group(2)
    answer = "".join(seen.get(tag, "?") for tag, _ in NAMES)
    tail = (r.stderr.strip().splitlines() or [""])[-1]
    return r.returncode, answer, errors, tail, r.stdout


def judge(case, trees, tmp):
    """None se o caso passa; senao, o que deu errado."""
    label, probe, tree, flags, (kind, what) = case
    code, answer, errors, tail, expansion = preprocess(flags, PROBES[probe], trees[tree], tmp)
    if kind == "erro":
        if code == 0 or not errors:
            return f"era para parar num #error ({what!r}) e foi aceito{'' if probe == 'wired' else ' como ' + answer}" if code == 0 else \
                   f"era para parar num #error ({what!r}) e o cpp falhou por outro motivo: {tail}"
        if len(errors) != 1 or what not in errors[0]:
            return f"parou, mas no #error errado ({what!r} esperado): {errors}"
        return None
    if code != 0 or errors:
        return f"era para ser aceito como {what} e parou: {errors or tail or code}"
    if probe == "wired":
        need, forbid = what
        missing = [t for t in need if t not in expansion]
        present = [t for t in forbid if t in expansion]
        if missing or present:
            return f"aceito, mas o WiredLink.cpp expandiu sem {missing} / com {present}"
        return None
    if answer != what:
        return f"aceito, mas WIFI/LAN/CYW43/ENC28J60 = {answer} em vez de {what}"
    return None


def run_all(cases, trees, tmp):
    return [(c[0], judge(c, trees, tmp)) for c in cases]


# ---------------------------------------------------------------------------------------------
# O segundo driver cabeado. A receita esta no comentario do NetCaps.h; aqui ela e aplicada, linha
# de diretiva a linha de diretiva (o comentario cita as mesmas expressoes e nao pode ser tocado).
# ---------------------------------------------------------------------------------------------
def replace_in_directives(text, old, new):
    """`old` por `new` em UMA linha de diretiva do texto; levanta se forem zero ou mais de uma."""
    lines = text.split("\n")
    hits = [i for i, ln in enumerate(lines) if ln.lstrip().startswith("#") and old in ln]
    if len(hits) != 1:
        raise ValueError(f"{old!r} esta em {len(hits)} diretivas do NetCaps.h, esperava 1: a receita do comentario mudou?")
    lines[hits[0]] = lines[hits[0]].replace(old, new)
    return "\n".join(lines)


def add_second_driver(text):
    """O NetCaps.h com um W5500 de brinquedo, pelos quatro passos que o comentario manda: o macro de
    identidade com o default 0, o termo dele no default do CYW43, o termo dele na conta, e a classe."""
    lines = text.split("\n")
    at = [i for i, ln in enumerate(lines) if ln.strip().startswith("#define SIMUT_NET_ENC28J60 0")]
    if len(at) != 1 or not lines[at[0] + 1].strip().startswith("#endif"):
        raise ValueError("o default do ENC28J60 no NetCaps.h nao e um #ifndef/#define/#endif: a receita do comentario mudou?")
    lines[at[0] + 2:at[0] + 2] = ["#ifndef SIMUT_NET_W5500", "#define SIMUT_NET_W5500 0", "#endif"]
    text = "\n".join(lines)
    text = replace_in_directives(text, "(!SIMUT_NET_ENC28J60)", "(!SIMUT_NET_ENC28J60 && !SIMUT_NET_W5500)")
    text = replace_in_directives(text, "(SIMUT_NET_CYW43 + SIMUT_NET_ENC28J60)",
                                 "(SIMUT_NET_CYW43 + SIMUT_NET_ENC28J60 + SIMUT_NET_W5500)")
    return replace_in_directives(text, "#define SIMUT_NET_LAN (SIMUT_NET_ENC28J60)",
                                 "#define SIMUT_NET_LAN (SIMUT_NET_ENC28J60 || SIMUT_NET_W5500)")


def write_tree(base, name, config_text, caps_text):
    root = os.path.join(base, name)
    os.makedirs(os.path.join(root, "net"), exist_ok=True)
    with open(os.path.join(root, "simut_config.h"), "w", encoding="utf-8") as fh:
        fh.write(config_text)
    with open(os.path.join(root, "net", "NetCaps.h"), "w", encoding="utf-8") as fh:
        fh.write(caps_text)
    return root


def add_device_elif(cpp_text):
    """O WiredLink.cpp com o `#elif` do W5500 de brinquedo e o include do cabecalho de dispositivo dele, onde a
    receita manda: na cadeia de includes, antes do `#else` que para num #error."""
    needle = '#include "Enc28j60Device.h"\n#else\n'
    if cpp_text.count(needle) != 1:
        raise ValueError("a cadeia de includes do WiredLink.cpp nao e '#if ... #include \"Enc28j60Device.h\" #else': "
                         "a receita do comentario do NetCaps.h mudou?")
    return cpp_text.replace(needle, '#include "Enc28j60Device.h"\n#elif SIMUT_NET_W5500\n#include "W5500Device.h"\n#else\n')


def write_wired_tree(base, name, caps_text, cpp_text, toy_device=False):
    """Uma arvore para pre-processar o WiredLink.cpp: o NetCaps.h dado, o simut_config.h reduzido a incluir o NetCaps.h
    (o de verdade para num #error de OTA antes do que se quer ver), os cabecalhos de verdade do src/net/, o `cpp_text` no
    lugar do WiredLink.cpp, e arquivos vazios no lugar do que so o compilador cruzado tem (Arduino.h, IPAddress.h e a
    biblioteca do ENC28J60): o pre-processador so precisa achar os includes."""
    root = os.path.join(base, name)
    os.makedirs(os.path.join(root, "net"), exist_ok=True)
    files = {"simut_config.h": '#pragma once\n#include "net/NetCaps.h"\n',
             "Arduino.h": "", "IPAddress.h": "", "PicoENC28J60lwIP.h": "",
             os.path.join("net", "NetCaps.h"): caps_text,
             os.path.join("net", "WiredLink.cpp"): cpp_text}
    for f in ("WiredLink.h", "StaticAddress.h", "Enc28j60Device.h"):
        with open(os.path.join(SRC, "net", f), encoding="utf-8") as fh:
            files[os.path.join("net", f)] = fh.read()
    if toy_device:
        files[os.path.join("net", "W5500Device.h")] = "#pragma once\nTOY_W5500_DEVICE\n"
    for rel, text in files.items():
        with open(os.path.join(root, rel), "w", encoding="utf-8") as fh:
            fh.write(text)
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


# ---------------------------------------------------------------------------------------------
# O leitor de flags do PlatformIO: tools/check_cli_help.py roda em todo `pio run` e acha, nos flags
# da imagem, se ela e a cabeada. Roda aqui com um `env` de brinquedo que so devolve BUILD_FLAGS.
# ---------------------------------------------------------------------------------------------
def cli_help_lan(flags):
    """(o LAN que o check_cli_help.py leu, o que ele disse, codigo de saida)."""
    script = os.path.join(ROOT, "tools", "check_cli_help.py")
    code = (
        f"FLAGS = {list(flags)!r}\n"
        "class Env:\n"
        "    def get(self, key, default=None):\n"
        "        return FLAGS if key == 'BUILD_FLAGS' else default\n"
        f"    def subst(self, text):\n        return {ROOT!r}\n"
        "g = {'Import': lambda *a: None, 'env': Env(), '__name__': 'check_cli_help'}\n"
        f"exec(compile(open({script!r}, encoding='utf-8').read(), {script!r}, 'exec'), g)\n"
        "print('HARNESS_LAN=%d' % int(bool(g['LAN'])))\n"
    )
    r = subprocess.run([sys.executable, "-B", "-c", code], capture_output=True, text=True)
    m = re.search(r"HARNESS_LAN=(\d)", r.stdout)
    return (None if m is None else m.group(1) == "1"), (r.stdout + r.stderr).strip(), r.returncode


def main():
    if shutil.which("cpp") is None:
        print("FALHOU cpp do host disponivel\n    instale o gcc: o teste pre-processa o cabecalho")
        return 1
    fails = []
    M = gf.load_manifest()

    if not os.path.isfile(CAPS):
        print("test_net_caps: FALHOU")
        print(f"  - {os.path.relpath(CAPS, ROOT)} nao existe: a identidade e a classe da rede ainda estao no simut_config.h")
        return 1
    with open(CAPS, encoding="utf-8") as fh:
        caps_text = fh.read()
    with open(CONFIG, encoding="utf-8") as fh:
        config_text = fh.read()
    with open(WIRED_CPP, encoding="utf-8") as fh:
        wired_text = fh.read()

    # 2. O cabecalho e puro e e o unico que define a classe.
    code = strip_comments(caps_text)
    if re.search(r"^\s*#\s*include\b", code, re.M):
        fails.append("o NetCaps.h tem um #include: ele nao pode ter, para ser seguro nas suites do host")
    loose = [ln for ln in code.split("\n") if ln.strip() and not ln.lstrip().startswith("#") and not ln.rstrip().endswith("\\")]
    if loose:
        fails.append(f"o NetCaps.h tem codigo alem de diretivas: {loose[:3]}")
    if "#pragma once" not in code:
        fails.append("o NetCaps.h sem #pragma once: incluido duas vezes ele recusaria a propria classe")
    definers = {}
    for dirpath, _dirs, files in os.walk(SRC):
        for f in files:
            if f.endswith((".h", ".hpp", ".cpp", ".c", ".ino")):
                path = os.path.join(dirpath, f)
                with open(path, encoding="utf-8", errors="ignore") as fh:
                    for n, ln in enumerate(fh, 1):
                        if re.match(r"\s*#\s*define\s+SIMUT_NET_(LAN|WIFI)\b", ln):
                            definers.setdefault(os.path.relpath(path, SRC), []).append(n)
    if sorted(definers) != [os.path.join("net", "NetCaps.h")] or [len(v) for v in definers.values()] != [2]:
        fails.append(f"a classe (SIMUT_NET_LAN, SIMUT_NET_WIFI) tem de ser definida so no net/NetCaps.h, uma vez cada: {definers}")

    tmp = tempfile.mkdtemp(prefix="net_caps_")
    bitten = 0
    try:
        def build_trees(cfg_text, cap_text, tag, cpp_text=None):
            """As arvores de includes: a real (so o que o src/ tem, ou a copia mutante dele), a com o segundo driver, e as
            tres do WiredLink.cpp (o de verdade com o ENC28J60; com o segundo driver e sem cabecalho de dispositivo; com ele)."""
            cpp_text = wired_text if cpp_text is None else cpp_text
            try:
                second = add_second_driver(cap_text)
            except ValueError as e:
                fails.append(str(e))
                second = cap_text
            try:
                with_header = add_device_elif(cpp_text)
            except ValueError as e:
                fails.append(str(e))
                with_header = cpp_text
            real = SRC if (cfg_text is config_text and cap_text is caps_text) else write_tree(tmp, f"real_{tag}", cfg_text, cap_text)
            return {"real": real, "segundo": write_tree(tmp, f"segundo_{tag}", cfg_text, second),
                    "cabo": write_wired_tree(tmp, f"cabo_{tag}", cap_text, cpp_text),
                    "segundo_cabo": write_wired_tree(tmp, f"segundo_cabo_{tag}", second, cpp_text),
                    "segundo_cabo_ok": write_wired_tree(tmp, f"segundo_cabo_ok_{tag}", second, with_header, toy_device=True)}

        trees = build_trees(config_text, caps_text, "base")

        # 1. As respostas, pelo NetCaps.h, pelo simut_config.h e pela receita do segundo driver.
        baseline_ok = True
        for label, why in run_all(CASES, trees, tmp):
            if why:
                baseline_ok = False
                fails.append(f"{label}: {why}")

        # 4. Cada #error morde. Os do NetCaps.h saem do NetCaps.h (e da copia que a receita faz dele); o do
        #    simut_config.h sai do simut_config.h; o da cadeia de includes sai do WiredLink.cpp.
        error_lines = [ln for ln in caps_text.split("\n") if ln.startswith("#error")]
        for token in CAPS_ERRORS:
            if sum(token in ln for ln in error_lines) != 1:
                fails.append(f"o #error de {token!r} esta em {sum(token in ln for ln in error_lines)} linhas do NetCaps.h, esperava 1")
        stranger = [ln for ln in error_lines if not any(t in ln for t in CAPS_ERRORS)]
        if stranger:
            fails.append(f"#error do NetCaps.h sem caso: {stranger}")
        if sum(ERR_OTA in ln for ln in config_text.split("\n") if ln.startswith("#error")) != 1:
            fails.append(f"o #error de {ERR_OTA!r} nao esta em exatamente uma linha do simut_config.h")
        wired_errors = [ln for ln in wired_text.split("\n") if ln.startswith("#error")]
        if [ln for ln in wired_errors if ERR_DEVICE in ln] == [] or len(wired_errors) != 1:
            fails.append(f"o WiredLink.cpp tem de ter um #error so, o de {ERR_DEVICE!r}, no fim da cadeia de includes: {wired_errors}")
        all_errors = CAPS_ERRORS + [ERR_OTA, ERR_DEVICE]
        for token in all_errors:
            if not [c for c in CASES if c[4] == ("erro", token)]:
                fails.append(f"o #error de {token!r} nao tem caso que o provoque")
        if baseline_ok:
            for token in all_errors:
                try:
                    tag = "m_" + str(abs(hash(token)))
                    if token == ERR_OTA:
                        mutant = build_trees(blank_error(config_text, token), caps_text, tag)
                    elif token == ERR_DEVICE:
                        mutant = build_trees(config_text, caps_text, tag, cpp_text=blank_error(wired_text, token))
                    else:
                        mutant = build_trees(config_text, blank_error(caps_text, token), tag)
                except ValueError as e:
                    fails.append(str(e))
                    continue
                failed = [label for label, why in run_all(CASES, mutant, tmp) if why]
                want = [c[0] for c in CASES if c[4] == ("erro", token)]
                if sorted(failed) == sorted(want):
                    bitten += 1
                else:
                    fails.append(f"sem o #error de {token!r}, falharam {failed or 'nenhum caso'}; era para falhar so {want}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # 5. A classe nunca vai por -D, e o leitor de flags do PlatformIO acha a classe pela identidade.
    class_flag = re.compile(r"-DSIMUT_NET_(LAN|WIFI)\b")
    for path in ("platformio.ini", os.path.join("tools", "generated", "profiles.ini")):
        with open(os.path.join(ROOT, path), encoding="utf-8") as fh:
            hit = [ln.strip() for ln in fh if class_flag.search(ln)]
        if hit:
            fails.append(f"{path} passa a classe da rede por -D (o NetCaps.h a recusa): {hit}")
    cli_cases = 0
    for name in M["profiles"]:
        prof = gf.resolve_profile(name, M["profiles"])
        flags = gf.compose(prof, M)["flags"]
        if [f for f in flags if class_flag.match(f)]:
            fails.append(f"compose( ) de {name} passa a classe da rede por -D: {[f for f in flags if class_flag.match(f)]}")
        want_lan = M["net"][prof["net"]]["class"] == "lan"
        got, said, rc = cli_help_lan(flags)
        cli_cases += 1
        if got is None or rc != 0:
            fails.append(f"check_cli_help.py nao rodou com os flags de {name} (saida {rc}): {said[-300:]}")
        elif got != want_lan:
            fails.append(f"check_cli_help.py leu LAN={got} nos flags de {name}, e a imagem {'e' if want_lan else 'nao e'} a cabeada: "
                         f"o leitor de flags tem de achar a classe pela identidade do driver")

    if fails:
        print("test_net_caps: FALHOU")
        for f in fails:
            print("  -", f)
        return 1
    print(f"test_net_caps: {len(CASES)} montagens (pelo NetCaps.h, pelo simut_config.h e com um segundo driver cabeado) "
          f"julgadas pelo pre-processador; {bitten} de {len(CAPS_ERRORS) + 2} #error mordem (sem a linha, exatamente os casos dele "
          f"falham); {cli_cases} imagens do manifesto sem a classe por -D e com o check_cli_help.py lendo a classe certa.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
