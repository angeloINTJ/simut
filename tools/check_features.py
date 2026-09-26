#!/usr/bin/env python3
"""check_features.py — o portao de fidelidade do modelo de recursos (P1).

Prova que tools/generated/profiles.ini, gerado do manifesto features.toml,
resolve para EXATAMENTE a mesma configuracao de build que os [env:*] escritos a
mao no platformio.ini de hoje. Compara, por ambiente de firmware:

    - o conjunto de macros -D e demais flags de compilacao;
    - o conjunto de unidades de traducao compiladas (o filtro resolvido contra src/);
    - o lib_ignore;
    - custom_web_omit, custom_fs_pages e build_type.

A resolucao dos dois lados e feita pelo proprio PlatformIO
(`pio project config --json-output`), que expande extends e ${...} — nao por um
parser proprio que poderia discordar do que o build realmente ve.

Nao compila nada. Saida 1 em qualquer divergencia.

Uso:
    python3 tools/check_features.py
"""
from __future__ import annotations

import fnmatch
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
PLATFORMIO_INI = os.path.join(ROOT, "platformio.ini")

sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_features  # noqa: E402


def pio_config(project_dir: str) -> dict:
    """Roda o resolvedor do PlatformIO e devolve {secao: {opcao: valor}}."""
    r = subprocess.run(
        ["pio", "project", "config", "--json-output", "-d", project_dir],
        capture_output=True, text=True,
    )
    if r.returncode != 0:
        sys.stderr.write(r.stdout + "\n" + r.stderr + "\n")
        raise SystemExit(f"pio project config falhou em {project_dir}")
    return {sec: dict(opts) for sec, opts in json.loads(r.stdout)}


def src_text() -> str:
    """Todo o texto de src/, para conferir presenca de macro."""
    buf = []
    for dirpath, _dirs, files in os.walk(SRC):
        for f in files:
            if f.endswith((".cpp", ".c", ".h", ".hpp", ".ino")):
                with open(os.path.join(dirpath, f), errors="ignore") as fh:
                    buf.append(fh.read())
    return "\n".join(buf)


def check_manifest(M: dict) -> tuple[list[str], list[str]]:
    """Sanidade do manifesto: macros SIMUT_ existem em src/ (a morta e anotada),
    e todo arquivo citado nas listas de filtro existe. Devolve (erros, notas)."""
    errors, notes = [], []
    text = src_text()

    # macros SIMUT_ declaradas -> {macro: dead?}
    simut: dict[str, bool] = {}
    for t in M["toggles"].values():
        mac = t.get("macro", "")
        if mac.startswith("SIMUT_"):
            simut[mac] = bool(t.get("dead"))
    for disp in M["display"].values():
        for fl in disp.get("flags", []):
            if fl.startswith("-DSIMUT_"):
                simut.setdefault(fl[2:].split("=")[0], False)
    for mac, dead in sorted(simut.items()):
        present = mac in text
        if not present and not dead:
            errors.append(f"macro {mac} do manifesto nao aparece em src/")
        elif not present and dead:
            notes.append(f"{mac} e um flag morto (nao lido em src/), mantido so "
                         f"por fidelidade; P0 o remove do manifesto e do platformio.ini")

    # arquivos citados nas listas de exclusao/inclusao existem
    referenced: set[str] = set()
    for disp in M["display"].values():
        referenced |= set(disp.get("exclude", [])) | set(disp.get("include", []))
    for t in M["toggles"].values():
        referenced |= set(t.get("off_exclude", [])) | set(t.get("on_include", []))
    for ref in sorted(referenced):
        if "*" in ref:
            import glob
            if not glob.glob(os.path.join(SRC, ref)):
                errors.append(f"padrao '{ref}' do manifesto nao casa arquivo em src/")
        elif not os.path.exists(os.path.join(SRC, ref)):
            errors.append(f"arquivo '{ref}' do manifesto nao existe em src/")
    return errors, notes


def source_units() -> list[str]:
    """Todas as unidades de traducao sob src/, como caminhos posix relativos."""
    out = []
    for dirpath, _dirs, files in os.walk(SRC):
        for f in files:
            if f.endswith((".cpp", ".c", ".ino")):
                rel = os.path.relpath(os.path.join(dirpath, f), SRC)
                out.append(rel.replace(os.sep, "/"))
    return out


def match(rel: str, pat: str) -> bool:
    """Casamento do build_src_filter: '*' nao cruza '/'."""
    if "/" in pat:
        return fnmatch.fnmatch(rel, pat)
    return "/" not in rel and fnmatch.fnmatch(rel, pat)


def resolve_filter(tokens: list[str], units: list[str]) -> set[str]:
    """Reduz um build_src_filter ao conjunto de unidades compiladas."""
    chosen: set[str] = set()
    for tok in tokens:
        if tok.startswith("+<") and tok.endswith(">"):
            pat, add = tok[2:-1], True
        elif tok.startswith("-<") and tok.endswith(">"):
            pat, add = tok[2:-1], False
        else:
            continue
        hit = {u for u in units if match(u, pat)}
        chosen |= hit if add else set()
        if not add:
            chosen -= hit
    return chosen


def parse_flags(flags: list[str]) -> tuple[dict, frozenset]:
    """Separa flags em macros (-D nome=valor) e o resto."""
    macros, other = {}, set()
    for f in flags:
        if f.startswith("-D"):
            body = f[2:]
            name, _, val = body.partition("=")
            macros[name] = val if "=" in body else None
        else:
            other.add(f)
    return macros, frozenset(other)


def norm_fs_pages(v) -> frozenset:
    if not v:
        return frozenset()
    return frozenset(p.strip() for p in str(v).split(",") if p.strip())


def tokens(values: list) -> list[str]:
    """Achata por espaco. O platformio.ini junta o ultimo flag do pico_base com
    os do env na mesma linha fisica (`${pico_base.build_flags} -D...`), e o
    resolvedor devolve isso como UM elemento; o gerado poe cada flag numa linha.
    O compilador separa por espaco, entao esta e a comparacao fiel."""
    return " ".join(str(v) for v in values).split()


def facet(opts: dict, units: list[str]) -> dict:
    macros, other = parse_flags(tokens(opts.get("build_flags", [])))
    return {
        "macros": macros,
        "other_flags": other,
        "units": resolve_filter(tokens(opts.get("build_src_filter", [])), units),
        "lib_ignore": frozenset(opts.get("lib_ignore", [])),
        "web_omit": opts.get("custom_web_omit") or None,
        "fs_pages": norm_fs_pages(opts.get("custom_fs_pages")),
        "build_type": opts.get("build_type") or None,
    }


def diff_facets(g: dict, x: dict) -> list[str]:
    out = []
    gm, xm = g["macros"], x["macros"]
    only_g = {k: gm[k] for k in gm if xm.get(k, "\0") != gm[k]}
    only_x = {k: xm[k] for k in xm if gm.get(k, "\0") != xm[k]}
    if only_g or only_x:
        out.append(f"    macros so no platformio.ini: {only_g or '{}'}")
        out.append(f"    macros so no gerado:         {only_x or '{}'}")
    if g["other_flags"] != x["other_flags"]:
        out.append(f"    flags nao-macro divergem: "
                   f"+{sorted(g['other_flags'] - x['other_flags'])} "
                   f"-{sorted(x['other_flags'] - g['other_flags'])}")
    if g["units"] != x["units"]:
        out.append(f"    unidades so no platformio.ini: {sorted(g['units'] - x['units'])}")
        out.append(f"    unidades so no gerado:         {sorted(x['units'] - g['units'])}")
    for key in ("lib_ignore", "fs_pages"):
        if g[key] != x[key]:
            out.append(f"    {key}: platformio={sorted(g[key])} gerado={sorted(x[key])}")
    for key in ("web_omit", "build_type"):
        if g[key] != x[key]:
            out.append(f"    {key}: platformio={g[key]!r} gerado={x[key]!r}")
    return out


def slice_section(text: str, name: str) -> str:
    """Extrai o bloco [name] verbatim, ate a proxima secao de topo."""
    lines = text.splitlines()
    start = next(i for i, ln in enumerate(lines) if ln.strip() == f"[{name}]")
    end = len(lines)
    for i in range(start + 1, len(lines)):
        if lines[i].startswith("[") and lines[i].rstrip().endswith("]"):
            end = i
            break
    return "\n".join(lines[start:end]).rstrip() + "\n"


def write_temp_project(envs_ini: str, tmp: str) -> None:
    """platformio.ini de teste: [platformio] + o [pico_base] real + os envs gerados.

    O pico_base e fatiado verbatim (o PlatformIO ja o analisa no arquivo real);
    os extra_scripts que ele cita nao rodam na resolucao de config.
    """
    pio_text = open(PLATFORMIO_INI).read()
    base = slice_section(pio_text, "pico_base")
    head = f"[platformio]\nsrc_dir = {SRC}\n\n"
    with open(os.path.join(tmp, "platformio.ini"), "w") as fh:
        fh.write(head + base + "\n" + envs_ini)


def main() -> int:
    manifest = gen_features.load_manifest()
    profiles = list(manifest["profiles"].keys())

    manifest_errors, manifest_notes = check_manifest(manifest)
    if manifest_errors:
        print("check_features: o manifesto tem referencias invalidas:")
        for e in manifest_errors:
            print(f"  {e}")
        return 1

    ground = pio_config(ROOT)
    # os perfis do manifesto devem ser exatamente os ambientes pico_w_* de hoje
    ground_fw = {s[4:] for s in ground if s.startswith("env:pico_w_")}
    missing = ground_fw - set(profiles)
    extra = set(profiles) - ground_fw
    if missing or extra:
        print("check_features: o manifesto nao cobre os ambientes de firmware:")
        if missing:
            print(f"  no platformio.ini e ausentes do manifesto: {sorted(missing)}")
        if extra:
            print(f"  no manifesto e ausentes do platformio.ini: {sorted(extra)}")
        return 1

    ini, _js = gen_features.build_outputs(manifest)
    units = source_units()

    with tempfile.TemporaryDirectory(dir="/tmp") as tmp:
        write_temp_project(ini, tmp)
        generated = pio_config(tmp)

    findings = []
    for name in profiles:
        g = facet(ground[f"env:{name}"], units)
        x = facet(generated[f"env:{name}"], units)
        d = diff_facets(g, x)
        if d:
            findings.append((name, d))

    if findings:
        print("check_features: os perfis gerados NAO reproduzem o platformio.ini:\n")
        for name, d in findings:
            print(f"  [env:{name}]")
            print("\n".join(d))
            print()
        return 1

    print(f"check_features: {len(profiles)} ambientes de firmware reproduzidos "
          f"do manifesto, byte a byte de configuracao:")
    for name in profiles:
        n = len(facet(ground[f"env:{name}"], units)["units"])
        print(f"  env:{name:<20} {n} unidades de traducao")
    for note in manifest_notes:
        print(f"  nota: {note}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
