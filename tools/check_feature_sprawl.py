#!/usr/bin/env python3
"""check_feature_sprawl.py — a marca d'agua do emaranhamento (P2 de MODELO_DE_RECURSOS.md).

Conta, por macro de recurso e por arquivo, os sitios de pre-processador em src/
que testam uma macro SIMUT_ (`#if`, `#ifdef`, `#ifndef`, `#elif`), e os prende a
uma marca d'agua em tools/feature_sprawl.json. Acrescentar um sitio a um arquivo
compartilhado (o emaranhamento CRESCE) reprova o gate; extrair um recurso para a
sua propria unidade de traducao (o emaranhamento ENCOLHE) e o objetivo do P2 e
baixa a marca. E o mesmo mecanismo do orcamento de flash, aplicado ao
emaranhamento: o numero fica no diff, onde um revisor o ve.

O simut_config.h fica de fora: e onde as macros sao DEFINIDAS (os `#ifndef X /
#define X`), nao onde elas espalham codigo. A regra do P2 e "so aceita o numero
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

# simut_config.h define as macros; nao conta como espalhamento.
SKIP_FILES = {"simut_config.h"}
EXTS = (".cpp", ".c", ".h", ".hpp", ".ino")

COND = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b(.*)$")
MACRO = re.compile(r"\bSIMUT_[A-Z0-9_]+\b")


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
                    m = COND.match(line)
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
    print(f"emaranhamento: {grand} sitios de #if SIMUT_ em {len(macs)} macros")
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
    print(f"check_feature_sprawl: {grand} sitios de #if SIMUT_, nenhum acima da "
          f"marca d'agua.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
