#!/usr/bin/env python3
"""gen_features.py — o gerador do modelo de recursos (P1 de MODELO_DE_RECURSOS.md).

Le tools/features.toml (a fonte unica) e escreve, sem tocar em mais nada:

    tools/generated/profiles.ini    um [env:*] por perfil, para o PlatformIO
    docs/configurador/model.json    a arvore do configurador: produtos, chaves,
                                    regras medidas, avisos, tetos e custos por
                                    produto (tools/feature_costs.json), e as chaves
                                    que o gerador recusa em cada produto
    src/FeatureBits.h               SIMUT_FEATURE_BITS, o "feat" do /api/status:
                                    um bit por chave, lido dos macros que o
                                    compilador ve (tools/test_feature_bits.py)
    docs/FEATURES.md, .pt-BR, .es-ES
                                    a tabela "nucleo e chaves", entre marcadores:
                                    cada chave, o que faz e quais imagens
                                    publicadas a ligam; o resto do arquivo e a mao

Antes de gerar, confere o manifesto: toda chave tem rotulo, todo grupo existe,
toda regra fala de chaves conhecidas — e nenhum dos sete perfis que o CI
constroi quebra uma regra. rule_violations( ) e a semantica das regras que
tools/build_custom.py reaplica antes de compilar e que a pagina espelha.

Cada [env:*] gerado herda `pico_base` do platformio.ini e acrescenta apenas os
flags, o filtro de fontes e o lib_ignore que o manifesto deriva dos recursos
ligados. O platformio.ini inclui o profiles.ini via `extra_configs` e nao define
mais [env:pico_w_*]; tools/check_features.py prova que os perfis que o build usa
resolvem para o que o manifesto descreve.

Uso:
    python3 tools/gen_features.py            # escreve os arquivos gerados
    python3 tools/gen_features.py --stdout   # imprime o profiles.ini sem escrever
    python3 tools/gen_features.py --check     # falha se o gerado esta desatualizado

Nao ha dependencia externa: tomllib e da biblioteca padrao (Python 3.11+).
"""
from __future__ import annotations

import argparse
import itertools
import json
import os
import re
import sys
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "tools", "features.toml")
OUT_INI = os.path.join(ROOT, "tools", "generated", "profiles.ini")
OUT_MODEL = os.path.join(ROOT, "docs", "configurador", "model.json")
OUT_HEADER = os.path.join(ROOT, "src", "FeatureBits.h")
COSTS = os.path.join(ROOT, "tools", "feature_costs.json")
LANGS = ("en", "pt")

# A tabela das chaves nos tres FEATURES*.md — o A-07 da revisao externa da v2.7.3,
# que pedia o mapa do que e nucleo e do que e opcional. Os textos sao os do
# configurador; o espanhol, que a pagina nao mostra, mora no mesmo manifesto
# (label.es / help.es). Sem o custo em flash, de proposito: ele e medido numa data
# e sobre uma base, e quem o mostra com as duas e o configurador.
OUT_FEATURES = {
    "en": os.path.join(ROOT, "docs", "FEATURES.md"),
    "pt": os.path.join(ROOT, "docs", "FEATURES.pt-BR.md"),
    "es": os.path.join(ROOT, "docs", "FEATURES.es-ES.md"),
}
SWITCHES_BEGIN = "<!-- BEGIN generated: switches — tools/gen_features.py -->"
SWITCHES_END = "<!-- END generated: switches -->"
SWITCH_WORDS = {
    "en": {"head": ("Group", "Switch", "What it does"), "on": "on", "off": "off"},
    "pt": {"head": ("Grupo", "Chave", "O que faz"), "on": "sim", "off": "não"},
    "es": {"head": ("Grupo", "Interruptor", "Qué hace"), "on": "sí", "off": "no"},
}

# Ordem canonica dos interruptores na emissao. So afeta a legibilidade do
# profiles.ini; o portao compara conjuntos, nao ordem.
TOGGLE_ORDER = [
    "cli_full", "web_https", "license_stub", "mdns",
    "concurrency_asserts", "bluetooth", "air", "sound_buzzer",
    "sensor_ds18b20", "sensor_dht22", "sensor_bme280",
    "tel_tls", "tel_mqtt",
    "web_history", "web_export_api", "web_metrics", "syslog",
    "tft_graph",
    "lan",
]

HEADER = """; profiles.ini — GERADO por tools/gen_features.py a partir de tools/features.toml.
; NAO EDITE A MAO. Rode `python3 tools/gen_features.py` depois de mudar o manifesto.
;
; Este arquivo E a fonte dos nove ambientes de firmware: o platformio.ini o inclui
; via `extra_configs` e nao define mais [env:pico_w_*] nem [env:pico2_w_*].
; tools/gen_features.py --check garante que ele esta em dia com o manifesto, e
; tools/check_features.py prova que ele resolve, flag a flag e unidade de traducao a
; unidade, para o que o manifesto descreve. A troca foi validada por build
; byte-a-byte contra a imagem anterior (docs/analysis/MODELO_DE_RECURSOS.md, P1).
"""


def load_manifest() -> dict:
    with open(MANIFEST, "rb") as fh:
        return tomllib.load(fh)


def resolve_profile(name: str, profiles: dict) -> dict:
    """Achata a cadeia de `extends` num dicionario de recursos."""
    prof = profiles[name]
    if "extends" not in prof:
        return dict(prof)
    merged = resolve_profile(prof["extends"], profiles)
    merged.update({k: v for k, v in prof.items() if k != "extends"})
    return merged


def dedup(seq: list) -> list:
    seen, out = set(), []
    for x in seq:
        if x not in seen:
            seen.add(x)
            out.append(x)
    return out


def toggle_refusals(prof: dict, M: dict) -> list[tuple[str, str]]:
    """As chaves LIGADAS que o chip do perfil nao pode compilar: [(chave, motivo)].

    Uma chave pode trocar a placa do ambiente (`on_board`: chip -> placa, a placa
    daquele chip com a chave ligada). Um chip sem entrada nao tem placa para a
    chave, e o gerador a recusa em vez de compor a placa do chip sem a chave --
    uma imagem que diria uma coisa e seria outra."""
    chip = prof.get("chip", "rp2040")
    out = []
    for tname in TOGGLE_ORDER:
        boards = M["toggles"][tname].get("on_board")
        if boards is not None and prof.get(tname, False) and chip not in boards:
            out.append((tname, f"{tname} ligada nao tem placa para o chip {chip}: "
                               f"[toggles.{tname}].on_board so conhece {', '.join(sorted(boards))}"))
    return out


def toggle_refusal(prof: dict, M: dict) -> str | None:
    """O primeiro motivo de toggle_refusals( ), ou None. compose( ) o aplica; o
    build_custom.py e o measure_savings.py o perguntam antes, porque a entrada deles
    nao e so os perfis do manifesto; e o model.json leva, por produto, as chaves
    cuja inversao ele recusa, para a pagina travar a chave como trava uma regra."""
    refused = toggle_refusals(prof, M)
    return refused[0][1] if refused else None


def compose(prof: dict, M: dict) -> dict:
    """Deriva a config do PlatformIO a partir dos recursos de um perfil."""
    flags = list(M["base"]["common_flags"])
    excludes: list[str] = []
    includes: list[str] = []
    lib_ignore: list[str] = []
    lib_deps: list[str] = []      # bibliotecas que so uma chave ligada traz (on_lib_deps)
    unflags: list[str] = []

    why = toggle_refusal(prof, M)
    if why:
        sys.exit(f"gen_features: o perfil nao compoe: {why}")

    # The chip first: it decides the board, and its flags are about the
    # toolchain, not about a feature. A profile without the key is the Pico W.
    chip_name = prof.get("chip", "rp2040")
    chip = M["chip"][chip_name]
    flags += chip.get("flags", [])
    board = chip.get("board", "")
    board_owner = None

    disp = M["display"][prof["display"]]
    flags += disp.get("flags", [])
    excludes += disp.get("exclude", [])
    includes += disp.get("include", [])
    if disp.get("ignore_display_libs"):
        lib_ignore += M["libs"]["display"]
    # The web builder's @IF blocks to cut (tools/build_webui_gz.py): the display
    # choice may name one, a switch that is off names its own (off_web_omit), and
    # a switch that is on names what only its other side has (on_web_omit: the
    # radio's fields on a wired build). So may the chip: what only another chip has
    # does not reach this one's pages.
    web_omit = [disp["web_omit"]] if disp.get("web_omit") else []
    if chip.get("web_omit"):
        web_omit.append(chip["web_omit"])

    for tname in TOGGLE_ORDER:
        t = M["toggles"][tname]
        val = bool(prof.get(tname, False))
        macro = t.get("macro", "")
        emit = t.get("emit", "none")
        if emit == "value":
            flags.append(f"-D{macro}={1 if val else 0}")
        elif emit == "on_is_1":
            if val:
                flags.append(f"-D{macro}=1")
        elif emit == "off_is_0":
            if not val:
                flags.append(f"-D{macro}=0")
        elif emit == "bare":
            if val:
                flags.append(f"-D{macro}")
        if val:
            flags += t.get("on_flags", [])
            includes += t.get("on_include", [])
            # A flag of the base that this switch takes back out (build_unflags),
            # and the board it asks for on this chip (toggle_refusal( ) above
            # guarantees the entry). Two switches asking for different boards
            # cannot both be obeyed: refuse, never let the later one win.
            unflags += t.get("on_unflags", [])
            lib_deps += t.get("on_lib_deps", [])
            if t.get("on_web_omit"):
                web_omit.append(t["on_web_omit"])
            if "on_board" in t:
                asked = t["on_board"][chip_name]
                if board_owner is not None and asked != board:
                    sys.exit(f"gen_features: {board_owner} e {tname} pedem placas diferentes "
                             f"para o chip {chip_name}: {board} e {asked}")
                board, board_owner = asked, tname
        else:
            excludes += t.get("off_exclude", [])
            lib_ignore += t.get("off_lib_ignore", [])
            if t.get("off_web_omit"):
                web_omit.append(t["off_web_omit"])

    # Not a switch: which roots the image trusts for its next update. Only an
    # unpublished profile asks for the bench root, and check_manifest( ) refuses
    # it anywhere else (docs/analysis/OTA_ASSINADA.md).
    if prof.get("ota_trust_bench"):
        flags.append("-DSIMUT_OTA_TRUST_BENCH=1")

    return {
        "flags": flags,
        "excludes": dedup(excludes),   # a switch may name a unit its display already drops
        "includes": includes,
        "lib_ignore": dedup(lib_ignore),
        "lib_deps": dedup(lib_deps),
        "web_omit": ", ".join(web_omit) or None,
        "custom_fs_pages": prof.get("custom_fs_pages"),
        "build_type": prof.get("build_type"),
        "board": board,
        "unflags": dedup(unflags),
        # A chip that boots its program from a slot of a partition table links
        # it with a script of its own, and gives LittleFS the size its table does
        # (the RP2350: tools/rp2350/, docs/analysis/OTA_AB_RP2350.md step 3).
        "ldscript": chip.get("ldscript", ""),
        "filesystem_size": chip.get("filesystem_size", ""),
    }


def emit_env(name: str, c: dict) -> str:
    lines = [f"[env:{name}]", "extends = pico_base"]
    if c.get("board"):
        lines.append(f"board = {c['board']}")
    if c.get("ldscript"):
        lines.append(f"board_build.ldscript = {c['ldscript']}")
    if c.get("filesystem_size"):
        lines.append(f"board_build.filesystem_size = {c['filesystem_size']}")
    if c["build_type"]:
        lines.append(f"build_type = {c['build_type']}")
    # build_flags
    fl = ["build_flags = ${pico_base.build_flags}"]
    fl += [f"    {f}" for f in c["flags"]]
    lines += fl
    # Flags of pico_base that this environment takes back out. Absent, and not an
    # empty `build_unflags =`, when there is none: the environments that have no
    # use for it keep the text they always had.
    if c.get("unflags"):
        lines += ["build_unflags ="] + [f"    {f}" for f in c["unflags"]]
    # build_src_filter
    bf = ["build_src_filter = ${pico_base.build_src_filter}"]
    bf += [f"    -<{f}>" for f in c["excludes"]]
    bf += [f"    +<{f}>" for f in c["includes"]]
    lines += bf
    # lib_ignore (explicito: envs que declaram lib_ignore substituem a lista)
    lib = ["lib_ignore ="]
    lib += [f"    {x}" for x in c["lib_ignore"]]
    lines += lib
    # Bibliotecas que uma chave traz: as do pico_base e mais estas. Ausente quando
    # nenhuma chave pede, e as imagens sem a chave nem baixam a biblioteca.
    if c.get("lib_deps"):
        lines += ["lib_deps = ${pico_base.lib_deps}"] + [f"    {x}" for x in c["lib_deps"]]
    if c["web_omit"]:
        lines.append(f"custom_web_omit = {c['web_omit']}")
    if c["custom_fs_pages"]:
        lines.append("custom_fs_pages = " + ", ".join(c["custom_fs_pages"]))
    return "\n".join(lines) + "\n"


def config_of(prof: dict) -> dict:
    """O que as regras enxergam de um perfil: o mostrador e cada chave, em bool."""
    cfg = {"display": prof["display"]}
    for t in TOGGLE_ORDER:
        cfg[t] = bool(prof.get(t, False))
    return cfg


def _matches(cfg: dict, conds: dict) -> bool:
    return all(cfg.get(k) == v for k, v in conds.items())


def rule_violations(cfg: dict, M: dict) -> list[dict]:
    """As regras que `cfg` quebra: todas as condicoes de `when` valem e alguma
    de `require` nao. E a mesma semantica de docs/configurador/rules.js —
    tools/test_configurator_rules.py roda as duas contra a mesma tabela."""
    return [r for r in M.get("rules", [])
            if _matches(cfg, r["when"]) and not _matches(cfg, r["require"])]


def hazard_hits(cfg: dict, M: dict) -> list[dict]:
    """Os avisos que `cfg` acende: compila, mas carrega um risco conhecido."""
    return [h for h in M.get("hazards", []) if _matches(cfg, h["when"])]


def _texts(entry: dict, key: str, where: str) -> dict:
    t = entry.get(key)
    if not isinstance(t, dict) or any(not str(t.get(lang, "")).strip() for lang in LANGS):
        sys.exit(f"gen_features: {where}.{key} precisa de texto em " + " e ".join(LANGS))
    return {lang: t[lang] for lang in LANGS}


def budget_why(r: dict, M: dict, without: str | None = None) -> dict:
    """As palavras de um bloqueio do orcamento (tools/pio_budget.py refusals( )), em cada
    lingua: o problema ([ui_budget.<id>] `why`) e o remedio, as chaves de `fix` (as que,
    desligadas uma de cada vez, fazem caber) pelos rotulos de [ui_toggles], em `fix` ou,
    com um item so, em `fix_one`. `without` e a trava de uma chave: ela sai da lista,
    porque desligar a chave que se quer ligar nao e remedio (a trava do DS18B20 no SIMUT
    Wire mandava desligar o proprio DS18B20, 2026-10-13); sem nenhuma, fica o problema."""
    entry = M.get("ui_budget", {}).get(r["id"])
    if entry is None:
        sys.exit(f"gen_features: o orcamento recusa com '{r['id']}', e o manifesto nao tem [ui_budget.{r['id']}]")
    where = f"ui_budget.{r['id']}"
    why, fix, one = (_texts(entry, k, where) for k in ("why", "fix", "fix_one"))
    keys = [k for k in r["fix"] if k != without]
    out = {}
    for lang in LANGS:
        names = [M["ui_toggles"][k]["label"][lang] for k in keys]
        text = why[lang]
        if names:
            text += " " + (fix if len(names) > 1 else one)[lang].replace("{list}", "; ".join(names))
        if "pin" in r:
            text = text.replace("{pin}", str(r["pin"]))
        if "{" in text:
            sys.exit(f"gen_features: {where} deixou um marcador sem valor: {text!r}")
        out[lang] = text
    return out


def blockers(prof: dict, M: dict) -> list[dict]:
    """Tudo o que impede a build de um perfil, na ordem em que a pagina e o CI o dizem:
    as regras que ele quebra, as chaves ligadas que o chip dele recusa (unsupported-<chave>)
    e o orcamento de PIO e de pinos (tools/pio_budget.py: pio-budget, pin-conflict). Cada
    item e {"id", "kind": "rule" | "unsupported" | "budget", "why": {en, pt}, "detail"}.

    Uma lista so (2026-10-13), onde o build_custom.py, o measure_savings.py e este gerador
    perguntavam cada um a sua parte. A pagina tem a mesma, docs/configurador/logic.js
    blockersOf( ), e tools/test_configurator_rules.py segura as duas na mesma tabela."""
    import pio_budget as pb  # so aqui: o resto do gerador nao precisa dele

    cfg = config_of(prof)
    out = [{"id": r["id"], "kind": "rule", "why": _texts(r, "why", f"rules.{r['id']}"), "detail": ""}
           for r in rule_violations(cfg, M)]
    for t, reason in toggle_refusals(prof, M):
        out.append({"id": f"unsupported-{t}", "kind": "unsupported",
                    "why": _texts(M["ui_toggles"][t], "unsupported", f"ui_toggles.{t}"), "detail": reason})
    for r in pb.refusals(cfg, prof.get("chip") or "rp2040"):
        out.append({"id": r["id"], "kind": "budget", "why": budget_why(r, M), "detail": r["reason"]})
    return out


def budget_table(M: dict) -> list[dict]:
    """O orcamento de PIO e de pinos para a pagina, como dado: cada combinacao das chaves
    que tools/pio_budget.py le (BUDGET_KEYS, e o mostrador para os pinos) que nao cabe num
    chip, com o motivo. A pagina so casa `when` com a escolha, como casa uma regra
    (rules.js matches), e nao conta bloco nenhum: o algoritmo mora num lugar so."""
    import pio_budget as pb

    out, seen = [], set()
    for chip in pb.CHIPS:
        for display in M["display"]:
            for bits in itertools.product((False, True), repeat=len(pb.BUDGET_KEYS)):
                cfg = dict(zip(pb.BUDGET_KEYS, bits), display=display)
                for r in pb.refusals(cfg, chip):
                    # Os blocos PIO nao dependem do mostrador; os pinos dependem.
                    when = dict(zip(pb.BUDGET_KEYS, bits))
                    if r["id"] != "pio-budget":
                        when["display"] = display
                    # Uma entrada por recusa: dois pinos em conflito na mesma combinacao sao
                    # duas, e o motivo (o GPIO) as separa; sem ele a pagina mostrava uma so.
                    key = (chip, r["id"], json.dumps(when, sort_keys=True), r["reason"])
                    if key in seen:
                        continue
                    seen.add(key)
                    # `lock`: o texto da trava de cada chave do remedio, sem ela na lista.
                    out.append({"id": r["id"], "chip": chip, "when": when,
                                "why": budget_why(r, M), "detail": r["reason"], "fix": r["fix"],
                                "lock": {k: budget_why(r, M, without=k) for k in r["fix"]}})
    return out


def check_manifest(M: dict) -> None:
    """Confere o manifesto antes de gerar. Um erro aqui e uma arvore que a pagina
    nao saberia desenhar, ou uma regra que reprova uma imagem que o CI constroi."""
    toggles, ui_t = M["toggles"], M.get("ui_toggles", {})
    groups, ui_d, ui_p = M.get("ui_groups", {}), M.get("ui_displays", {}), M.get("ui_products", {})
    for t in TOGGLE_ORDER:
        if t not in toggles:
            sys.exit(f"gen_features: {t} esta no TOGGLE_ORDER sem [toggles.{t}]")
        if t not in ui_t:
            sys.exit(f"gen_features: [toggles.{t}] sem [ui_toggles.{t}]: a arvore nao saberia rotula-la")
    for t in toggles:
        if t not in TOGGLE_ORDER:
            sys.exit(f"gen_features: [toggles.{t}] fora do TOGGLE_ORDER: nunca seria emitida")
    # O bit e contrato com quem le o /api/status: um app instalado decodifica o
    # numero, entao dois nomes no mesmo bit, ou um bit que nao cabe no uint32 que
    # o firmware imprime, e um cliente lendo a chave errada.
    owner = {}
    for t in TOGGLE_ORDER:
        b = toggles[t].get("bit")
        if type(b) is not int or not 0 <= b < 32:
            sys.exit(f"gen_features: [toggles.{t}] precisa de um bit inteiro de 0 a 31 "
                     f"(o \"feat\" do /api/status), e tem {b!r}")
        if b in owner:
            sys.exit(f"gen_features: bit {b} repetido em [toggles.{owner[b]}] e [toggles.{t}]: "
                     f"um bit e de uma chave so, para sempre")
        owner[b] = t
    for t, u in ui_t.items():
        if t not in toggles:
            sys.exit(f"gen_features: [ui_toggles.{t}] sem [toggles.{t}]")
        if u.get("group") not in groups:
            sys.exit(f"gen_features: ui_toggles.{t}.group '{u.get('group')}' nao e um [ui_groups]")
    for d in M["display"]:
        if d not in ui_d:
            sys.exit(f"gen_features: [display.{d}] sem [ui_displays.{d}]")
    for name in M["profiles"]:
        if name not in ui_p:
            sys.exit(f"gen_features: [profiles.{name}] sem [ui_products.{name}]")
        # Uma imagem publicada que confia na raiz de bancada aceitaria pelo ar o que
        # a chave menos guardada assina: no campo, isso e porta dos fundos.
        prof = resolve_profile(name, M["profiles"])
        if prof.get("ota_trust_bench") and prof.get("publish"):
            sys.exit(f"gen_features: o perfil {name} e publicado e pede ota_trust_bench: "
                     f"so imagem de bancada confia na raiz de bancada")
        chip = resolve_profile(name, M["profiles"]).get("chip", "rp2040")
        if chip not in M.get("chip", {}):
            sys.exit(f"gen_features: o perfil {name} pede o chip '{chip}', que nao tem [chip.{chip}]")
    # on_board e on_unflags mexem no ambiente do PlatformIO, nao so numa macro:
    # um chip que nao existe, ou uma lista que nao e de flags, so apareceria
    # como uma build que falha ou, pior, que compila outra coisa.
    for t in TOGGLE_ORDER:
        boards = toggles[t].get("on_board")
        if boards is not None:
            if (not isinstance(boards, dict) or not boards
                    or any(c not in M.get("chip", {}) or not isinstance(b, str) or not b
                           for c, b in boards.items())):
                sys.exit(f"gen_features: [toggles.{t}].on_board tem de ser {{chip = \"placa\"}} "
                         f"com chips que tem [chip.*]")
        # A chave que o gerador recusa em algum chip tem de dizer por que, nas duas linguas
        # da pagina: e o motivo da trava (model.json, products[].refused).
        if boards is not None:
            _texts(ui_t[t], "unsupported", f"ui_toggles.{t}")
        deps = toggles[t].get("on_lib_deps")
        if deps is not None:
            # Fixada por commit: um ramo ou uma tag que anda faria a mesma imagem
            # sair diferente amanha, e o CI nao reproduziria o build de hoje.
            if not isinstance(deps, list) or not all(isinstance(d, str) and re.search(r"#[0-9a-f]{40}$", d)
                                                     for d in deps):
                sys.exit(f"gen_features: [toggles.{t}].on_lib_deps tem de ser uma lista de bibliotecas "
                         f"fixadas por commit (url#<sha de 40 hexadecimais>)")
        # One @IF name each, as build_webui_gz.py reads custom_web_omit: a list or a
        # comma would cut more than the switch means, and a name it does not know
        # stops the build there, far from the manifest line that caused it.
        for k in ("on_web_omit", "off_web_omit"):
            v = toggles[t].get(k)
            if v is not None and not (isinstance(v, str) and re.fullmatch(r"[a-z][a-z0-9_]*", v)):
                sys.exit(f"gen_features: [toggles.{t}].{k} tem de ser um nome de @IF do "
                         f"build_webui_gz.py (minusculas, digitos e _), nao {v!r}")
        unflags = toggles[t].get("on_unflags")
        if unflags is not None and (not isinstance(unflags, list)
                                    or any(not isinstance(f, str) or not f.startswith("-") for f in unflags)):
            sys.exit(f"gen_features: [toggles.{t}].on_unflags tem de ser uma lista de flags (-D...)")
    known = set(TOGGLE_ORDER) | {"display"}
    for kind, parts in (("rules", ("when", "require")), ("hazards", ("when",))):
        ids = set()
        for r in M.get(kind, []):
            if r["id"] in ids:
                sys.exit(f"gen_features: {kind} com id repetido: {r['id']}")
            ids.add(r["id"])
            for part in parts:
                for k, v in r[part].items():
                    if k not in known:
                        sys.exit(f"gen_features: {kind} {r['id']}.{part}: chave desconhecida '{k}'")
                    if k == "display" and v not in M["display"]:
                        sys.exit(f"gen_features: {kind} {r['id']}.{part}: mostrador desconhecido '{v}'")
                    if k != "display" and not isinstance(v, bool):
                        sys.exit(f"gen_features: {kind} {r['id']}.{part}.{k} tem de ser true/false")
    # Nenhuma imagem que o CI constroi pode quebrar uma regra: se quebrasse, ou a
    # regra esta errada, ou o CI estaria compilando algo que nao linka.
    for name in M["profiles"]:
        bad = rule_violations(config_of(resolve_profile(name, M["profiles"])), M)
        if bad:
            sys.exit(f"gen_features: o perfil {name} quebra a regra {bad[0]['id']}")
    # Nem cair num bloqueio do chip ou do orcamento de PIO e de pinos (2026-10-13): uma
    # imagem que compila e nao cabe nos blocos PIO sobe com um sensor sem programa.
    import pio_budget as pb
    for name in M["profiles"]:
        prof = resolve_profile(name, M["profiles"])
        chip = prof.get("chip") or "rp2040"
        if chip not in pb.CHIPS:
            sys.exit(f"gen_features: o perfil {name} diz chip {chip!r}; tools/pio_budget.py conhece "
                     + ", ".join(pb.CHIPS))
        bad = blockers(prof, M)
        if bad:
            sys.exit(f"gen_features: o perfil {name} e bloqueado por {bad[0]['id']}: {bad[0]['detail'] or bad[0]['why']['en']}")
    # As palavras de cada recusa do orcamento, inclusive a que nenhuma imagem acende hoje: o
    # texto de um id so era conferido quando uma combinacao da tabela o produzia.
    ub = M.get("ui_budget", {})
    for k in pb.BUDGET_KEYS:
        if k not in M["toggles"] or k not in M.get("ui_toggles", {}):
            sys.exit(f"gen_features: tools/pio_budget.py le a chave {k}, que o manifesto nao tem com rotulo")
    for rid in pb.REFUSAL_IDS:
        if rid not in ub:
            sys.exit(f"gen_features: tools/pio_budget.py recusa com '{rid}', e o manifesto nao tem [ui_budget.{rid}]")
        if any("{list}" in s for s in _texts(ub[rid], "why", f"ui_budget.{rid}").values()):
            sys.exit(f"gen_features: ui_budget.{rid}.why e o problema; a lista vai em fix")
        for k in ("fix", "fix_one"):
            if any("{list}" not in s for s in _texts(ub[rid], k, f"ui_budget.{rid}").values()):
                sys.exit(f"gen_features: ui_budget.{rid}.{k} precisa de {{list}}, onde entram as chaves a desligar")
        # Montado com uma, duas e nenhuma chave, nada pode sobrar entre chaves: o pin-conflict
        # so aparece numa tabela de pinos que o firmware recusaria, e nunca seria montado.
        for fix in (pb.BUDGET_KEYS[:1], pb.BUDGET_KEYS[:2], ()):
            budget_why({"id": rid, "fix": list(fix), "pin": 0}, M)
    for rid in ub:
        if rid not in pb.REFUSAL_IDS:
            sys.exit(f"gen_features: [ui_budget.{rid}] nao e uma recusa de tools/pio_budget.py: "
                     + ", ".join(pb.REFUSAL_IDS))


def build_model(M: dict) -> dict:
    """A arvore do configurador, na forma que a pagina (docs/configurador/) le."""
    import check_flash_budget  # os mesmos tetos que o portao de flash usa

    check_manifest(M)
    costs = None
    if os.path.exists(COSTS):
        with open(COSTS, encoding="utf-8") as fh:
            costs = json.load(fh)

    groups = sorted(
        ({"id": g, "order": v["order"], "advanced": bool(v.get("advanced", False)),
          "label": _texts(v, "label", f"ui_groups.{g}")}
         for g, v in M["ui_groups"].items()),
        key=lambda g: g["order"])
    toggles = []
    for t in TOGGLE_ORDER:
        u = M["ui_toggles"][t]
        toggles.append({"id": t, "group": u["group"], "bit": M["toggles"][t]["bit"],
                        "label": _texts(u, "label", f"ui_toggles.{t}"),
                        "help": _texts(u, "help", f"ui_toggles.{t}")})
    displays = {d: {"label": _texts(M["ui_displays"][d], "label", f"ui_displays.{d}")}
                for d in M["display"]}
    products = []
    for name in M["profiles"]:
        u = M["ui_products"][name]
        prof = resolve_profile(name, M["profiles"])
        # Os tetos sao de cada produto (2026-10-05): o Pico 2 W e segurado ao
        # slot dele, 1.568.768 B, e o Pico W ao que o portao usa. Ate entao um
        # teto so, o do Pico W, valia para todos.
        # As chaves cuja inversao o gerador recusa neste produto (a rede por cabo no
        # RP2350, que nao tem placa para ela), com o motivo em en e pt: a pagina as
        # trava como trava uma chave que uma regra proibe.
        refused = {}
        for t in TOGGLE_ORDER:
            flipped = dict(prof)
            flipped[t] = not bool(prof.get(t, False))
            if any(name_ == t for name_, _ in toggle_refusals(flipped, M)):
                refused[t] = _texts(M["ui_toggles"][t], "unsupported", f"ui_toggles.{t}")
        products.append({"id": name, "order": u["order"],
                         "bench": bool(u.get("bench", False)),
                         "publish": bool(prof.get("publish", False)),
                         "chip": prof.get("chip") or "rp2040",
                         "refused": refused,
                         "ceilings": check_flash_budget.ceilings(name),
                         "label": _texts(u, "label", f"ui_products.{name}"),
                         "desc": _texts(u, "desc", f"ui_products.{name}"),
                         "config": config_of(prof)})
    products.sort(key=lambda p: p["order"])

    def public(r: dict, kind: str) -> dict:
        out = {"id": r["id"], "when": r["when"]}
        if kind == "rules":
            out["require"] = r["require"]
        out["why"] = _texts(r, "why", f"{kind}.{r['id']}")
        out["evidence"] = r["evidence"]
        return out

    return {
        "version": M["meta"]["version"],
        "source": "tools/features.toml",
        "groups": groups,
        "toggles": toggles,
        "displays": displays,
        "products": products,
        "rules": [public(r, "rules") for r in M.get("rules", [])],
        "hazards": [public(h, "hazards") for h in M.get("hazards", [])],
        "budget": budget_table(M),
        "costs": costs,
    }


def build_feature_header(M: dict) -> str:
    """src/FeatureBits.h: SIMUT_FEATURE_BITS, uma chave por bit.

    Cada bit sai do macro da chave com o mesmo teste para todos os modos de
    emissao: `defined(M) && M`. Le o macro de presenca (web_https, license_stub,
    concurrency_asserts: definido ligado, ausente desligado) e o 0/1 com default
    no simut_config.h do mesmo jeito, para todo flag que este gerador escreve."""
    check_manifest(M)
    lines = [
        "/**",
        " * @file FeatureBits.h",
        " * @brief SIMUT_FEATURE_BITS — which of the configurator's switches this image",
        " *        was built with. GENERATED by tools/gen_features.py from",
        " *        tools/features.toml: do not edit, change the manifest and run it.",
        " *",
        " * /api/status reports it as \"feat\", one bit per switch, so a client (the",
        " * simut-rx app, the fleet manager) hides what the image does not have instead",
        " * of meeting a 404 — as it already did for the display with \"cap\". A bit",
        " * never changes switch and is never reused: an installed app decodes it.",
        " *",
        " * The value comes from the macros the compiler sees, not from the manifest:",
        " * `defined(M) && M` reads a presence macro (set when on, absent when off) and",
        " * a 0/1 macro with a default in simut_config.h alike, for every flag the",
        " * generator writes. tools/test_feature_bits.py preprocesses this header with",
        " * each profile's flags, and each switch flipped from it, and compares.",
        " * (A hand-written -DSIMUT_WEB_HTTPS=0 switches the server ON under its",
        " * #ifdef — R-S27 — and would read here as off; the generator never writes it.)",
        " */",
        "#pragma once",
        '#include "simut_config.h"',
        "",
    ]
    names = []
    for t in sorted(TOGGLE_ORDER, key=lambda x: M["toggles"][x]["bit"]):
        b, macro = M["toggles"][t]["bit"], M["toggles"][t]["macro"]
        name = "SIMUT_FEAT_" + t.upper()
        names.append(name)
        lines += [f"#if defined({macro}) && {macro}",
                  f"#define {name} (1ul << {b})",
                  "#else",
                  f"#define {name} 0ul",
                  "#endif"]
    lines += ["", "#define SIMUT_FEATURE_BITS ( \\"]
    width = max(len(n) for n in names)
    lines += [f"    {n.ljust(width)} | \\" for n in names]
    lines += ["    0ul)", ""]
    return "\n".join(lines)


def _md_cell(text: str) -> str:
    """Texto do manifesto numa celula de tabela Markdown. O `<nome do aparelho>` do
    mDNS sumiria como uma tag HTML desconhecida, e um `|` partiria a linha."""
    return (text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
                .replace("|", "\\|"))


def build_switch_table(M: dict, lang: str) -> str:
    """As chaves por grupo, na ordem do manifesto, com o que cada imagem publicada
    liga — o mesmo `config_of` que as regras usam, entao chave ausente e desligada."""
    ui_t, groups, ui_p = M["ui_toggles"], M["ui_groups"], M.get("ui_products", {})
    pubs = [p for p, v in M["profiles"].items() if v.get("publish")]
    words = SWITCH_WORDS[lang]

    def text(entry: dict, key: str, where: str) -> str:
        t = entry.get(key, {})
        if not str(t.get(lang, "")).strip():
            sys.exit(f"gen_features: {where}.{key} precisa de texto em '{lang}' para a "
                     f"tabela de chaves dos FEATURES*.md")
        return t[lang]

    cfgs = {p: config_of(resolve_profile(p, M["profiles"])) for p in pubs}
    head = list(words["head"]) + [ui_p[p]["label"].get(lang, ui_p[p]["label"]["en"]) for p in pubs]
    lines = ["| " + " | ".join(head) + " |",
             "|" + "---|" * len(words["head"]) + ":---:|" * len(pubs)]
    for g in sorted(groups, key=lambda g: groups[g]["order"]):
        first = True
        for k, t in ui_t.items():
            if t["group"] != g:
                continue
            cells = [_md_cell(text(groups[g], "label", f"ui_groups.{g}")) if first else "",
                     _md_cell(text(t, "label", f"ui_toggles.{k}")),
                     _md_cell(text(t, "help", f"ui_toggles.{k}"))]
            cells += [words["on"] if cfgs[p][k] else words["off"] for p in pubs]
            lines.append("| " + " | ".join(cells) + " |")
            first = False
    return "\n".join(lines)


def with_switch_table(path: str, table: str) -> str:
    """O FEATURES*.md com a tabela trocada entre os marcadores; o resto, intacto."""
    text = open(path, encoding="utf-8").read() if os.path.exists(path) else ""
    a, b = text.find(SWITCHES_BEGIN), text.find(SWITCHES_END)
    if a < 0 or b < a:
        sys.exit(f"gen_features: {os.path.relpath(path, ROOT)} nao tem os marcadores "
                 f"da tabela de chaves ({SWITCHES_BEGIN} ... {SWITCHES_END})")
    return text[:a + len(SWITCHES_BEGIN)] + "\n" + table + "\n" + text[b:]


def build_outputs(M: dict) -> tuple[str, str]:
    ini_parts = [HEADER]
    for name in M["profiles"]:
        ini_parts.append(emit_env(name, compose(resolve_profile(name, M["profiles"]), M)))
    ini = "\n".join(ini_parts)
    model = json.dumps(build_model(M), indent=2, ensure_ascii=False) + "\n"
    return ini, model


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--stdout", action="store_true", help="imprime o profiles.ini")
    ap.add_argument("--check", action="store_true",
                    help="falha se os arquivos gerados estao desatualizados")
    args = ap.parse_args()

    M = load_manifest()
    ini, model = build_outputs(M)

    if args.stdout:
        sys.stdout.write(ini)
        return 0

    outputs = ((OUT_INI, ini), (OUT_MODEL, model), (OUT_HEADER, build_feature_header(M))) + tuple(
        (path, with_switch_table(path, build_switch_table(M, lang)))
        for lang, path in OUT_FEATURES.items())
    if args.check:
        stale = []
        for path, want in outputs:
            have = open(path, encoding="utf-8").read() if os.path.exists(path) else None
            if have != want:
                stale.append(os.path.relpath(path, ROOT))
        if stale:
            print("gen_features: desatualizado -> " + ", ".join(stale))
            print("rode: python3 tools/gen_features.py")
            return 1
        print("gen_features: os arquivos gerados estao em dia.")
        return 0

    for path, content in outputs:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(content)
    print("gen_features: escrito " + " e ".join(os.path.relpath(p, ROOT) for p, _ in outputs)
          + f" ({len(M['profiles'])} perfis).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
