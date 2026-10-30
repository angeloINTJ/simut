#!/usr/bin/env python3
"""check_feature_sprawl.py — a marca d'agua do emaranhamento (P2 de MODELO_DE_RECURSOS.md).

Conta, por macro de recurso e por arquivo, os sitios em src/ que testam uma macro
SIMUT_, e os prende a uma marca d'agua em tools/feature_sprawl.json. Um sitio e UMA
LINHA, lida sem o comentario (um `//` ate o fim da linha e um `/* ... */` nao contam,
nem a macro que citam; um `//` dentro de uma string nao e comentario; um literal de caractere como `'"'` nao abre string), de um destes
dois jeitos:
  - uma linha que comeca com `#` e `if`, `ifdef`, `ifndef` ou `elif`;
  - um `if constexpr` que ABRE a linha (depois de um `}` e de um `else`, se houver)
    com a condicao inteira nela (desde 2026-10-30; antes so os `#` contavam).
Cada macro de uma linha conta uma vez. NAO ve: um `if ( SIMUT_X )` comum, um ternario,
um `static_assert`, uma condicao partida em varias linhas; e, por ler linha a linha,
conta uma linha de comentario de varias linhas que comece por `#if` ou `if constexpr`.
tools/test_feature_sprawl.py segura cada item desta lista e prende o que ela diz que
nao ve: se um deles passar a contar, o teste diz, e este texto muda junto.
Acrescentar um sitio a um arquivo
compartilhado (o emaranhamento CRESCE) reprova o gate; extrair um recurso para a
sua propria unidade de traducao (o emaranhamento ENCOLHE) e o objetivo do P2 e
baixa a marca. E o mesmo mecanismo do orcamento de flash, aplicado ao
emaranhamento: o numero fica no diff, onde um revisor o ve.

O simut_config.h fica de fora: e onde as macros sao DEFINIDAS (os `#ifndef X /
#define X`), nao onde elas espalham codigo. O FeatureBits.h tambem: e gerado do
manifesto, um teste por chave. A regra do P2 e "so aceita o numero
descer", entao um sitio a mais e erro; ao extrair uma costura, rode --update para
travar o ganho no mesmo commit.

    python3 tools/check_feature_sprawl.py            # o gate
    python3 tools/check_feature_sprawl.py --update   # reescreve a marca d'agua
    python3 tools/check_feature_sprawl.py --report   # imprime o placar, nao falha
"""
from __future__ import annotations

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
BASELINE = os.path.join(ROOT, "tools", "feature_sprawl.json")

# simut_config.h define as macros; nao conta como espalhamento. O FeatureBits.h
# (2026-09-30) e gerado do manifesto pelo gen_features.py: um teste por chave,
# por construcao, para compor o "feat" do /api/status — e a tabela que o
# manifesto ja e, nao codigo espalhado. Contado, cada chave nova subiria a marca
# em um sitio sem que nada tivesse se emaranhado.
#
# O net/NetCaps.h (2026-10-13) tambem: e onde as macros da REDE sao definidas, a identidade
# (SIMUT_NET_CYW43, SIMUT_NET_ENC28J60) com o `#ifndef X / #define X` e a classe (SIMUT_NET_WIFI,
# SIMUT_NET_LAN) derivada dela. Contado, o cabecalho que separa a identidade da classe subiria as duas
# marcas em sitios que nao espalham nada: so definem.
#
# O ui/UiCaps.h (2026-10-14) tambem, pela mesma razao: e onde a UI do painel tem a identidade
# (SIMUT_UI_ANGULO, com o `#ifndef X / #define X`) e as capacidades derivadas dela (SIMUT_UI_ORIGINAL,
# SIMUT_UI_CUSTOM_THEMES), mais as duas checagens de #error: sitios que definem, nao que espalham.
#
# Os quatro cabecalhos de selecao do driver de UI (ui/UiDriver.h, UiDriverMembers.h, UiDriverHooks.h e
# UiDriverWeb.h; S4 de docs/analysis/UI_DRIVERS.md, 2026-10-15) tambem: e o unico lugar onde a classe do painel
# escolhe o driver da imagem (um `#if SIMUT_UI_ANGULO` em volta de um include), a funcao do UiCaps.h. Contados,
# tirar os `#if` do DisplayManager.h e do WebManager.h os poria em quatro arquivos e a marca subiria dois sitios
# sem que nada tivesse se emaranhado: o que sai do nucleo e o que este gate quer ver.
# O quinto (ui/UiDriverTouch.h, S10b C3, a cadeia de toque dentro de handleTouch) estava de fora ate 2026-10-30: seu `#if SIMUT_UI_ANGULO` contava
# como um sitio, movido do DisplayManager_Touch.cpp (a marca ficou em 362; feature_sprawl.json tinha "ui/UiDriverTouch.h": 1). Entrou na lista em S11 A5 (Q2 do
# plano do S11, decidida pela sessao principal e dita ao mantenedor): faz o mesmo que os quatro, escolher o driver da imagem num `#if SIMUT_UI_ANGULO` em volta
# de um include, e a marca desceu 1 (369 -> 368). Um sexto cabecalho continua sendo uma edicao aqui, de proposito (tools/test_feature_sprawl.py: 'a sixth header of that name is not let off').
SKIP_FILES = {"simut_config.h", "FeatureBits.h", "NetCaps.h", "UiCaps.h",
              "UiDriver.h", "UiDriverMembers.h", "UiDriverHooks.h", "UiDriverWeb.h", "UiDriverTouch.h"}
EXTS = (".cpp", ".c", ".h", ".hpp", ".ino")

COND = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b(.*)$")
# An `if constexpr ( SIMUT_X )` is a site as well (2026-10-30, S10c, o glifo de rede da barra de status): o mesmo desvio por um recurso, sem a linha de pre-processador que o mostrava; um arquivo
# ficava em um `#if` so porque o segundo desvio era escrito assim. Le-se, como o COND, so a linha que COMECA com ele (depois de um `else`, ou de um `}` e um `else`): uma linha de comentario
# comeca com `*` ou `//`, nao com `if`. tools/test_feature_sprawl.py segura a regra e os onze mutantes do portao.
CONSTEXPR = re.compile(r"^\s*(?:\}\s*)?(?:else\s+)?if\s+constexpr\b(.*)$")
MACRO = re.compile(r"\bSIMUT_[A-Z0-9_]+\b")


def tira_comentario(linha: str) -> str:
    """A linha sem os comentarios: um `/* ... */` e um `//` que vai ate o fim; o texto de uma string ou de um literal de caractere fica como esta (um `//` dentro de uma string nao e comentario, e a aspa de um `'"'` nao abre string). Um `/*` sem `*/` na linha corta o resto dela."""
    saida, i, n = [], 0, len(linha)
    while i < n:
        c, d = linha[i], linha[i + 1] if i + 1 < n else ""
        if c == '"':
            j = i + 1
            while j < n and linha[j] != '"':
                j += 2 if linha[j] == "\\" else 1
            saida.append(linha[i:j + 1])
            i = j + 1
        elif c == "'" and not (i and linha[i - 1].isalnum() and d.isalnum()):
            # um literal de caractere ('"', '\'', '\\') passa inteiro, para que a aspa dele nao abra uma string; um ' entre dois alfanumericos e separador de digitos (1'000), a regra do apaga( ) de tft_ui/test_rede.py
            j = i + 1
            while j < n and linha[j] != "'":
                j += 2 if linha[j] == "\\" else 1
            saida.append(linha[i:j + 1])
            i = j + 1
        elif c == "/" and d == "/":
            break
        elif c == "/" and d == "*":
            j = linha.find("*/", i + 2)
            if j < 0:
                break
            i = j + 2
        else:
            saida.append(c)
            i += 1
    return "".join(saida)


def scan() -> dict[str, dict[str, int]]:
    """{macro: {arquivo: n sitios de condicional}}."""
    counts: dict[str, dict[str, int]] = {}
    for dirpath, _dirs, files in os.walk(SRC):
        for f in files:
            if not f.endswith(EXTS) or f in SKIP_FILES:
                continue
            rel = os.path.relpath(os.path.join(dirpath, f), SRC).replace(os.sep, "/")
            with open(os.path.join(dirpath, f), errors="ignore") as fh:
                for line in fh:
                    linha = tira_comentario(line)
                    m = COND.match(linha) or CONSTEXPR.match(linha)
                    if not m:
                        continue
                    for mac in set(MACRO.findall(m.group(1))):
                        counts.setdefault(mac, {})
                        counts[mac][rel] = counts[mac].get(rel, 0) + 1
    return counts


def total(counts: dict[str, dict[str, int]], mac: str) -> int:
    return sum(counts.get(mac, {}).values())


def load_baseline() -> dict:
    if not os.path.exists(BASELINE):
        return {}
    return json.load(open(BASELINE))


def dump_baseline(counts: dict) -> str:
    ordered = {m: dict(sorted(counts[m].items())) for m in sorted(counts)}
    return json.dumps(ordered, indent=1, ensure_ascii=False) + "\n"


def report(counts: dict) -> None:
    macs = sorted(counts, key=lambda m: -total(counts, m))
    grand = sum(total(counts, m) for m in macs)
    print(f"emaranhamento: {grand} sitios de SIMUT_ (linhas # e if constexpr de uma linha) em {len(macs)} macros")
    for m in macs:
        files = counts[m]
        print(f"  {m:<28} {total(counts, m):>3} sitios / {len(files):>2} arquivos")


def main() -> int:
    args = set(sys.argv[1:])
    counts = scan()

    if "--report" in args:
        report(counts)
        return 0

    if "--update" in args:
        open(BASELINE, "w").write(dump_baseline(counts))
        report(counts)
        print(f"marca d'agua reescrita: {os.path.relpath(BASELINE, ROOT)}")
        return 0

    base = load_baseline()
    if not base:
        print("check_feature_sprawl: sem marca d'agua; rode --update primeiro.")
        return 1

    grew, shrank = [], []
    for mac in sorted(set(counts) | set(base)):
        cur = counts.get(mac, {})
        old = base.get(mac, {})
        for f in sorted(set(cur) | set(old)):
            c, o = cur.get(f, 0), old.get(f, 0)
            if c > o:
                grew.append(f"  {mac} em {f}: {o} -> {c} (+{c - o})")
            elif c < o:
                shrank.append(f"  {mac} em {f}: {o} -> {c} (-{o - c})")

    if grew:
        print("check_feature_sprawl: o emaranhamento CRESCEU (P2 so aceita descer):")
        print("\n".join(grew))
        print("Se e mesmo necessario, extraia o recurso para a sua unidade de "
              "traducao; se nao, rode --update e explique no commit por que subiu.")
        return 1

    if shrank:
        print("check_feature_sprawl: o emaranhamento encolheu (bom):")
        print("\n".join(shrank))
        print("Trave o ganho: rode `python3 tools/check_feature_sprawl.py --update` "
              "neste commit, para a marca descer junto.")
        return 1

    grand = sum(total(counts, m) for m in counts)
    print(f"check_feature_sprawl: {grand} sitios de SIMUT_ (linhas que comecam com # e `if constexpr` "
          f"de uma linha, sem o comentario), nenhum acima da marca d'agua.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
