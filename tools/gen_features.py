#!/usr/bin/env python3
"""gen_features.py — o gerador do modelo de recursos (P1 de MODELO_DE_RECURSOS.md).

Le tools/features.toml (a fonte unica) e escreve, sem tocar em mais nada:

    tools/generated/profiles.ini    um [env:*] por perfil, para o PlatformIO
    docs/configurador/model.json    a arvore do configurador: produtos, chaves,
                                    regras medidas, avisos, tetos e custos por
                                    produto (tools/feature_costs.json)

Antes de gerar, confere o manifesto: toda chave tem rotulo, todo grupo existe,
toda regra fala de chaves conhecidas — e nenhum dos seis perfis que o CI
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
import json
import os
import sys
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "tools", "features.toml")
OUT_INI = os.path.join(ROOT, "tools", "generated", "profiles.ini")
OUT_MODEL = os.path.join(ROOT, "docs", "configurador", "model.json")
COSTS = os.path.join(ROOT, "tools", "feature_costs.json")
BUDGET = os.path.join(ROOT, "tools", "flash_budget.json")
LANGS = ("en", "pt")

# Ordem canonica dos interruptores na emissao. So afeta a legibilidade do
# profiles.ini; o portao compara conjuntos, nao ordem.
TOGGLE_ORDER = [
    "cli_full", "web_https", "license_stub", "mdns",
    "concurrency_asserts", "bluetooth", "air", "sound_buzzer",
    "sensor_ds18b20", "sensor_dht22", "sensor_bme280",
    "tel_tls", "tel_mqtt",
]

HEADER = """; profiles.ini — GERADO por tools/gen_features.py a partir de tools/features.toml.
; NAO EDITE A MAO. Rode `python3 tools/gen_features.py` depois de mudar o manifesto.
;
; Este arquivo E a fonte dos seis ambientes de firmware: o platformio.ini o inclui
; via `extra_configs` e nao define mais [env:pico_w_*]. tools/gen_features.py --check
; garante que ele esta em dia com o manifesto, e tools/check_features.py prova que
; ele resolve, flag a flag e unidade de traducao a unidade, para o que o manifesto
; descreve. A troca foi validada por build byte-a-byte contra a imagem anterior
; (docs/analysis/MODELO_DE_RECURSOS.md, P1).
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


def compose(prof: dict, M: dict) -> dict:
    """Deriva a config do PlatformIO a partir dos recursos de um perfil."""
    flags = list(M["base"]["common_flags"])
    excludes: list[str] = []
    includes: list[str] = []
    lib_ignore: list[str] = []

    disp = M["display"][prof["display"]]
    flags += disp.get("flags", [])
    excludes += disp.get("exclude", [])
    includes += disp.get("include", [])
    if disp.get("ignore_display_libs"):
        lib_ignore += M["libs"]["display"]
    web_omit = disp.get("web_omit")

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
        else:
            excludes += t.get("off_exclude", [])
            lib_ignore += t.get("off_lib_ignore", [])

    return {
        "flags": flags,
        "excludes": excludes,
        "includes": includes,
        "lib_ignore": dedup(lib_ignore),
        "web_omit": web_omit,
        "custom_fs_pages": prof.get("custom_fs_pages"),
        "build_type": prof.get("build_type"),
    }


def emit_env(name: str, c: dict) -> str:
    lines = [f"[env:{name}]", "extends = pico_base"]
    if c["build_type"]:
        lines.append(f"build_type = {c['build_type']}")
    # build_flags
    fl = ["build_flags = ${pico_base.build_flags}"]
    fl += [f"    {f}" for f in c["flags"]]
    lines += fl
    # build_src_filter
    bf = ["build_src_filter = ${pico_base.build_src_filter}"]
    bf += [f"    -<{f}>" for f in c["excludes"]]
    bf += [f"    +<{f}>" for f in c["includes"]]
    lines += bf
    # lib_ignore (explicito: envs que declaram lib_ignore substituem a lista)
    lib = ["lib_ignore ="]
    lib += [f"    {x}" for x in c["lib_ignore"]]
    lines += lib
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


def build_model(M: dict) -> dict:
    """A arvore do configurador, na forma que a pagina (docs/configurador/) le."""
    import check_flash_budget  # o mesmo teto de OTA que o portao de flash usa

    check_manifest(M)
    with open(BUDGET, encoding="utf-8") as fh:
        flash_region = json.load(fh)["ceiling"]
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
        toggles.append({"id": t, "group": u["group"],
                        "label": _texts(u, "label", f"ui_toggles.{t}"),
                        "help": _texts(u, "help", f"ui_toggles.{t}")})
    displays = {d: {"label": _texts(M["ui_displays"][d], "label", f"ui_displays.{d}")}
                for d in M["display"]}
    products = []
    for name in M["profiles"]:
        u = M["ui_products"][name]
        prof = resolve_profile(name, M["profiles"])
        products.append({"id": name, "order": u["order"],
                         "bench": bool(u.get("bench", False)),
                         "publish": bool(prof.get("publish", False)),
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
        "ceilings": {"flash_region": flash_region,
                     "ota_bin": check_flash_budget.ota_safe_max()},
        "groups": groups,
        "toggles": toggles,
        "displays": displays,
        "products": products,
        "rules": [public(r, "rules") for r in M.get("rules", [])],
        "hazards": [public(h, "hazards") for h in M.get("hazards", [])],
        "costs": costs,
    }


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

    outputs = ((OUT_INI, ini), (OUT_MODEL, model))
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
