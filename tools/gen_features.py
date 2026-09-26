#!/usr/bin/env python3
"""gen_features.py — o gerador do modelo de recursos (P1 de MODELO_DE_RECURSOS.md).

Le tools/features.toml (a fonte unica) e escreve, sem tocar em mais nada:

    tools/generated/profiles.ini        um [env:*] por perfil, para o PlatformIO
    tools/generated/features_model.json  os recursos por perfil, para o configurador

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
OUT_JSON = os.path.join(ROOT, "tools", "generated", "features_model.json")

# Ordem canonica dos interruptores na emissao. So afeta a legibilidade do
# profiles.ini; o portao compara conjuntos, nao ordem.
TOGGLE_ORDER = [
    "cli_full", "web_https", "license_stub", "mdns",
    "concurrency_asserts", "bluetooth", "air", "sound_buzzer",
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


def build_outputs(M: dict) -> tuple[str, str]:
    profiles = M["profiles"]
    ini_parts = [HEADER]
    model = {"version": M["meta"]["version"], "profiles": {}}
    for name in profiles:
        prof = resolve_profile(name, profiles)
        c = compose(prof, M)
        ini_parts.append(emit_env(name, c))
        model["profiles"][name] = {
            "features": {k: v for k, v in prof.items()
                         if k not in ("custom_fs_pages",)},
            "derived": c,
        }
    ini = "\n".join(ini_parts)
    js = json.dumps(model, indent=2, ensure_ascii=False) + "\n"
    return ini, js


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--stdout", action="store_true", help="imprime o profiles.ini")
    ap.add_argument("--check", action="store_true",
                    help="falha se os arquivos gerados estao desatualizados")
    args = ap.parse_args()

    M = load_manifest()
    ini, js = build_outputs(M)

    if args.stdout:
        sys.stdout.write(ini)
        return 0

    if args.check:
        stale = []
        for path, want in ((OUT_INI, ini), (OUT_JSON, js)):
            have = open(path).read() if os.path.exists(path) else None
            if have != want:
                stale.append(os.path.relpath(path, ROOT))
        if stale:
            print("gen_features: desatualizado -> " + ", ".join(stale))
            print("rode: python3 tools/gen_features.py")
            return 1
        print("gen_features: os arquivos gerados estao em dia.")
        return 0

    os.makedirs(os.path.dirname(OUT_INI), exist_ok=True)
    with open(OUT_INI, "w") as fh:
        fh.write(ini)
    with open(OUT_JSON, "w") as fh:
        fh.write(js)
    print(f"gen_features: escrito {os.path.relpath(OUT_INI, ROOT)} e "
          f"{os.path.relpath(OUT_JSON, ROOT)} ({len(M['profiles'])} perfis).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
