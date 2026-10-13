#!/usr/bin/env python3
"""test_cli_gates.py — o avaliador das guardas #if que os portoes do console usam.

tools/check_cli_help.py e tools/check_air_consistency.py leem o parser do console
como texto e precisam saber que linhas uma imagem compila. tools/cli_gates.py avalia
os #if de quatro macros conhecidos e deixa intacto o que nao sabe avaliar. Este teste
prende a gramatica, o descarte de ramos e as recusas.

    python3 tools/test_cli_gates.py
"""
import copy
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cli_gates import eval_gate, strip_gated  # noqa: E402

falhas = []
casos = 0


def check(cond, msg):
    global casos
    casos += 1
    if not cond:
        falhas.append(msg)


def ev(expr, flags, want):
    got = eval_gate(expr, flags)
    check(got is want, f"eval_gate({expr!r}, {flags}) deu {got!r}, esperava {want!r}")


# Cada macro conhecido, com 0 e 1; um que falta vale 0.
for m in ("SIMUT_CLI_FULL", "SIMUT_AIR", "SIMUT_NET_LAN"):
    ev(m, {m: 1}, True)
    ev(m, {m: 0}, False)
    ev(m, {}, False)
# SIMUT_NET_WIFI deriva de SIMUT_NET_LAN, e o valor explicito vence a derivacao.
ev("SIMUT_NET_WIFI", {}, True)
ev("SIMUT_NET_WIFI", {"SIMUT_NET_LAN": 1}, False)
ev("SIMUT_NET_WIFI", {"SIMUT_NET_LAN": 0}, True)
ev("SIMUT_NET_WIFI", {"SIMUT_NET_LAN": 1, "SIMUT_NET_WIFI": 1}, True)
# Operadores, parenteses e precedencia do C.
ev("!SIMUT_AIR", {"SIMUT_AIR": 1}, False)
ev("SIMUT_CLI_FULL && SIMUT_AIR", {"SIMUT_CLI_FULL": 1, "SIMUT_AIR": 0}, False)
ev("SIMUT_CLI_FULL || SIMUT_NET_LAN", {"SIMUT_CLI_FULL": 0, "SIMUT_NET_LAN": 1}, True)
ev("1 || 0 && 0", {}, True)
ev("!1 || 1", {}, True)
ev("!(1 || 0)", {}, False)
ev("(SIMUT_CLI_FULL || SIMUT_AIR) && !SIMUT_NET_LAN", {"SIMUT_AIR": 1}, True)
ev("2", {}, True)
ev("0", {}, False)
ev("  SIMUT_AIR  ", {"SIMUT_AIR": 1}, True)
# Comentarios no fim.
ev("SIMUT_NET_WIFI   /* the cable needs no SSID */", {"SIMUT_NET_LAN": 1}, False)
ev("SIMUT_CLI_FULL // the full grammar", {"SIMUT_CLI_FULL": 1}, True)
# O que nao se avalia: None, sem excecao.
for ruim in ("defined(SIMUT_AIR)", "defined SIMUT_AIR", "SIMUT_FOO", "SIMUT_AIR == 1",
             "SIMUT_AIR + 1", "", "(SIMUT_AIR", "SIMUT_AIR)", "SIMUT_AIR &&", "&& SIMUT_AIR",
             "MAX_SENSORS < 12", "!"):
    try:
        got = eval_gate(ruim, {"SIMUT_AIR": 1})
    except Exception as e:  # noqa: BLE001
        got = f"excecao {type(e).__name__}"
    check(got is None, f"eval_gate({ruim!r}) deveria ser None, deu {got!r}")


def strip(src, flags):
    return strip_gated(src, flags)


# Bloco conhecido falso sai; verdadeiro fica, sem as diretivas.
src = "a\n#if SIMUT_AIR\nb\n#endif\nc\n"
check(strip(src, {"SIMUT_AIR": 0}) == "a\n\n\n\nc\n", "bloco falso: " + repr(strip(src, {"SIMUT_AIR": 0})))
check(strip(src, {"SIMUT_AIR": 1}) == "a\n\nb\n\nc\n", "bloco verdadeiro: " + repr(strip(src, {"SIMUT_AIR": 1})))
# Cadeias #if/#elif/#else escolhem cada ramo.
chain = "#if SIMUT_AIR\nA\n#elif SIMUT_NET_LAN\nL\n#else\nW\n#endif"
check(strip(chain, {"SIMUT_AIR": 1}) == "\nA\n\n\n\n\n", "cadeia, ramo #if: " + repr(strip(chain, {"SIMUT_AIR": 1})))
check(strip(chain, {"SIMUT_NET_LAN": 1}) == "\n\n\nL\n\n\n", "cadeia, ramo #elif: " + repr(strip(chain, {"SIMUT_NET_LAN": 1})))
check(strip(chain, {}) == "\n\n\n\n\nW\n", "cadeia, ramo #else: " + repr(strip(chain, {})))
# Desconhecido: o bloco inteiro fica, diretivas inclusive; o conhecido dentro dele e avaliado.
unk = "#if defined(X)\nx\n#if SIMUT_AIR\nair\n#endif\n#endif"
check(strip(unk, {"SIMUT_AIR": 0}) == "#if defined(X)\nx\n\n\n\n#endif",
      "desconhecido por fora, conhecido por dentro: " + repr(strip(unk, {"SIMUT_AIR": 0})))
# Um #elif desconhecido deixa o bloco inteiro.
mix = "#if SIMUT_AIR\nA\n#elif FOO\nF\n#endif"
check(strip(mix, {"SIMUT_AIR": 0}) == mix, "#elif desconhecido: " + repr(strip(mix, {"SIMUT_AIR": 0})))
# Desconhecido dentro de um ramo removido sai com ele.
nest = "#if SIMUT_AIR\n#if defined(X)\nx\n#endif\n#endif\nz"
check(strip(nest, {"SIMUT_AIR": 0}) == "\n\n\n\n\nz", "desconhecido num ramo removido: " + repr(strip(nest, {"SIMUT_AIR": 0})))
# #ifdef/#ifndef ficam inteiros.
d = "#ifdef SIMUT_AIR\nq\n#endif\n#ifndef SIMUT_AIR\nr\n#endif"
check(strip(d, {"SIMUT_AIR": 1}) == d, "#ifdef/#ifndef: " + repr(strip(d, {"SIMUT_AIR": 1})))
# Espacos antes do # e depois dele.
sp = "  #  if SIMUT_AIR\nb\n  # endif"
check(strip(sp, {"SIMUT_AIR": 0}) == "\n\n", "diretivas com espacos: " + repr(strip(sp, {"SIMUT_AIR": 0})))
# Desbalanceado: o que fica aberto fecha no fim; um #endif a mais fica como esta.
check(strip("#if SIMUT_AIR\nb", {"SIMUT_AIR": 0}) == "\n", "sem #endif")
check(strip("a\n#endif\nb", {}) == "a\n#endif\nb", "#endif a mais")
# O numero de linhas e o \n final se mantem.
for s in (src, chain, unk, nest, d, "x"):
    for f in ({}, {"SIMUT_AIR": 1}, {"SIMUT_NET_LAN": 1}):
        out = strip(s, f)
        check(out.count("\n") == s.count("\n"), f"linhas mudaram: {s!r} {f}")
# Nenhuma das duas altera o dicionario.
flags = {"SIMUT_NET_LAN": 1, "SIMUT_CLI_FULL": 0}
antes = copy.deepcopy(flags)
eval_gate("SIMUT_NET_WIFI || SIMUT_AIR", flags)
strip_gated(chain, flags)
check(flags == antes, "flags alterados")

# O caso de verdade: o parser do console.
real = ('if (t0 == "help") return CMD_HELP;\n'
        "#if SIMUT_NET_WIFI\n"
        'if (t0 == "ap") return CMD_AP;\n'
        "#endif\n"
        "#if SIMUT_CLI_FULL || SIMUT_NET_LAN\n"
        'if (t0 == "ip") return CMD_IP_CFG;\n'
        "#endif\n")
lan = strip(real, {"SIMUT_NET_LAN": 1, "SIMUT_CLI_FULL": 0})
wifi = strip(real, {"SIMUT_NET_LAN": 0, "SIMUT_CLI_FULL": 0})
check('"ap"' not in lan and '"ip"' in lan and '"help"' in lan, "parser, imagem cabeada: " + repr(lan))
check('"ap"' in wifi and '"ip"' not in wifi and '"help"' in wifi, "parser, imagem Wi-Fi: " + repr(wifi))

if falhas:
    print("test_cli_gates: FALHOU")
    for f in falhas:
        print("  " + f)
    sys.exit(1)
print(f"test_cli_gates: {casos} casos.")
