#!/usr/bin/env python3
"""build_custom.py — compila a build montada no configurador, com as regras reaplicadas.

A entrada é um perfil — o produto e as chaves que mudaram —, o formato que o
configurador (docs/configurador/) escreve:

    {"v": 1, "base": "pico_w_air", "set": {"mdns": true, "sensor_dht22": false}}

A saída, em --out (padrão build-custom/), é o firmware em dois formatos — o
.uf2, que se grava por USB (arrastar para a unidade RPI-RP2), e o .bin, que se
instala pela OTA da interface web — mais build.json com o perfil, os flags, os
tamanhos medidos e os sha256.

A entrada é NÃO CONFIÁVEL: chega por um workflow_dispatch que a página preenche,
e qualquer string que alcançasse o profiles.ini viraria configuração do
PlatformIO. Por isso a validação é estrita e vem antes de tudo: só as chaves do
manifesto, só true/false, só produtos que existem, tamanho limitado — e as
regras entre recursos (gen_features.rule_violations) reaplicadas aqui, porque a
página não é a autoridade.

Códigos de saída: 0 compilou; 2 perfil inválido (formato ou regra — nada foi
compilado); 3 não cabe no slot de flash; 1 falha de build.

    python3 tools/build_custom.py --profile '{"v":1,"base":"pico_w_release","set":{}}'
    SIMUT_PROFILE='...' python3 tools/build_custom.py      # como o CI chama
    SIMUT_PROFILE='...' python3 tools/build_custom.py --validate-only
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_features as gf  # noqa: E402
import measure_savings as ms  # noqa: E402  (TempEnvs, pio_build_full)

ROOT = gf.ROOT
ENV = "_custom"
MAX_SPEC = 4096          # um perfil real tem ~200 B; isto só barra abuso


class SpecError(ValueError):
    """Perfil recusado antes de qualquer compilação (exit 2)."""


def _no_duplicates(pairs):
    """json.loads guarda a ÚLTIMA de duas chaves iguais, calado: um perfil com
    "base" duas vezes seria compilado com uma e lido por gente com a outra."""
    out = {}
    for key, value in pairs:
        if key in out:
            raise SpecError(f"chave repetida no perfil: {key!r}")
        out[key] = value
    return out


def parse_spec(text, M):
    """Valida o perfil e devolve (base, set). Tudo que não for exatamente o
    formato esperado é recusado — nada é corrigido nem ignorado em silêncio."""
    if not isinstance(text, str) or not text.strip():
        raise SpecError("perfil vazio")
    if len(text) > MAX_SPEC:
        raise SpecError(f"perfil com {len(text)} caracteres (máximo {MAX_SPEC})")
    try:
        spec = json.loads(text, object_pairs_hook=_no_duplicates,
                          parse_constant=_refuse_constant)
    except json.JSONDecodeError as e:
        raise SpecError(f"perfil não é JSON: {e.msg}") from None
    if not isinstance(spec, dict):
        raise SpecError("perfil tem de ser um objeto JSON")
    extra = set(spec) - {"v", "base", "set"}
    if extra:
        raise SpecError("campo desconhecido no perfil: " + ", ".join(sorted(map(repr, extra))))
    if type(spec.get("v")) is not int or spec["v"] != 1:   # nem true, nem 1.0
        raise SpecError("versão do perfil tem de ser 1")
    base = spec.get("base")
    if not isinstance(base, str) or base not in M["profiles"]:
        raise SpecError(f"produto desconhecido: {base!r}")
    changes = spec.get("set", {})
    if not isinstance(changes, dict):
        raise SpecError("`set` tem de ser um objeto")
    for key, value in changes.items():
        if key not in gf.TOGGLE_ORDER:
            raise SpecError(f"chave desconhecida: {key!r}")
        if not isinstance(value, bool):
            raise SpecError(f"{key} tem de ser true ou false, não {value!r}")
    return base, dict(changes)


def _refuse_constant(name):
    """NaN e Infinity não são JSON (RFC 8259), mas o json do Python os aceita."""
    raise SpecError(f"perfil não é JSON: {name}")


def canonical(base, changes):
    """O perfil numa forma única — a mesma configuração dá sempre o mesmo nome."""
    return json.dumps({"v": 1, "base": base, "set": dict(sorted(changes.items()))},
                      sort_keys=True, separators=(",", ":"))


def resolve(base, changes, M):
    prof = gf.resolve_profile(base, M["profiles"])
    prof.update(changes)
    # Uma build do configurador vai para o campo, seja qual for o produto de base:
    # nunca confia na raiz de bancada, nem partindo de uma imagem de bancada
    # (docs/analysis/OTA_ASSINADA.md). `changes` so traz chaves do TOGGLE_ORDER.
    prof.pop("ota_trust_bench", None)
    return prof


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def say(msg, error=False):
    """Uma linha para quem lê o log. No GitHub Actions, um erro também vira
    anotação (::error::), que a página da execução mostra e a API devolve. O
    texto pode trazer pedaços do perfil, que não é confiável: escapado como o
    Actions pede, uma quebra de linha nele não vira comando novo."""
    # Nenhuma quebra de linha crua chega ao log: no Actions, uma linha que
    # começa com :: é um comando. As mensagens já usam repr( ) nos pedaços do
    # perfil; isto vale mesmo que uma mensagem futura esqueça.
    line = msg.replace("\r", "\\r").replace("\n", "\\n")
    print(f"build_custom: {line}")
    if error and os.environ.get("GITHUB_ACTIONS") == "true":
        esc = msg.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
        print(f"::error title=build_custom::{esc}")


def gh_output(**kv):
    """Saídas do step no GitHub Actions; fora do CI, não faz nada."""
    path = os.environ.get("GITHUB_OUTPUT")
    if path:
        with open(path, "a", encoding="utf-8") as fh:
            for k, v in kv.items():
                fh.write(f"{k}={v}\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profile", help="o perfil em JSON (senão, lê SIMUT_PROFILE)")
    ap.add_argument("--out", default="build-custom", help="pasta de saída")
    ap.add_argument("--validate-only", action="store_true",
                    help="só valida formato e regras; não compila")
    args = ap.parse_args()

    M = gf.load_manifest()
    text = args.profile if args.profile is not None else os.environ.get("SIMUT_PROFILE", "")
    try:
        base, changes = parse_spec(text, M)
    except SpecError as e:
        say(f"perfil recusado — {e}", error=True)
        return 2
    prof = resolve(base, changes, M)
    bad = gf.rule_violations(gf.config_of(prof), M)
    if bad:
        for r in bad:
            say(f"perfil recusado — regra {r['id']}: {r['why']['en']}", error=True)
        return 2
    spec = canonical(base, changes)
    tag = hashlib.sha256(spec.encode()).hexdigest()[:8]
    for h in gf.hazard_hits(gf.config_of(prof), M):
        say(f"aviso {h['id']}: {h['why']['en']}")
    if args.validate_only:
        say(f"perfil válido — {spec}")
        return 0

    c = gf.compose(prof, M)
    with ms.TempEnvs([gf.emit_env(ENV, c)]):
        res = ms.pio_build_full(ENV, echo=True)
    if "overflow" in res:
        say(f"não cabe no slot de flash — excede em {res['overflow']} B", error=True)
        gh_output(result="overflow", overflow=res["overflow"])
        return 3
    if "error" in res:
        say(f"a build falhou — {res['error']}", error=True)
        gh_output(result="error")
        return 1

    build_dir = os.path.join(ROOT, ".pio", "build", ENV)
    os.makedirs(args.out, exist_ok=True)
    name = f"simut_{base}_{tag}"
    files = {}
    for ext in ("uf2", "bin"):
        dst = os.path.join(args.out, f"{name}.{ext}")
        shutil.copyfile(os.path.join(build_dir, f"firmware.{ext}"), dst)
        files[ext] = {"file": os.path.basename(dst), "sha256": sha256(dst),
                      "bytes": os.path.getsize(dst)}
    import check_flash_budget  # o mesmo teto de OTA que o portão usa
    ota_max = check_flash_budget.ota_bin_max()   # o maior .bin que ainda cabe assinado
    fits_ota = ota_max is None or res["bin"] <= ota_max
    report = {
        "profile": json.loads(spec),
        "config": gf.config_of(prof),
        "flags": c["flags"],
        "excludes": c["excludes"],
        "sizes": {"flash_used": res["used"], "bin": res["bin"], "ram_used": res["ram"]},
        "ceilings": {"ota_bin": ota_max},
        "fits_ota": fits_ota,
        "files": files,
        # O commit de onde a build saiu, não o último que tocou em src/: o
        # checkout do Actions é raso (uma revisão), e lá os dois dariam o HEAD
        # de qualquer jeito. Com ele e o perfil, a build se refaz byte a byte
        # (conferido em 26/09: CI e máquina local, o mesmo sha256).
        "commit": subprocess.run(["git", "rev-parse", "HEAD"],
                                 cwd=ROOT, capture_output=True, text=True).stdout.strip(),
        # Só arquivo rastreado conta: a pasta de saída e os cabeçalhos gerados
        # da build são não rastreados e existiriam em toda build.
        "dirty": bool(subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"],
                                     cwd=ROOT, capture_output=True, text=True).stdout.strip()),
    }
    with open(os.path.join(args.out, "build.json"), "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2, ensure_ascii=False)
        fh.write("\n")
    say(f"{name} — flash {res['used']} B, .bin {res['bin']} B, RAM {res['ram']} B")
    if not fits_ota:
        say(f"aviso — o .bin passa do teto de OTA ({ota_max} B): grave pelo .uf2 (USB); a OTA recusaria")
    gh_output(result="ok", name=name, used=res["used"], bin=res["bin"],
              ram=res["ram"], fits_ota=str(fits_ota).lower())
    return 0


if __name__ == "__main__":
    sys.exit(main())
