#!/usr/bin/env python3
"""measure_savings.py — quanto cada recurso DEVOLVE de flash e RAM quando desligado.

CONTRATO (a regra do que este portao garante — escrito antes da implementacao):

  economia(F) = usado(base)  -  usado(base com SO o recurso F desligado)

  medida para os DOIS numeros que a imagem gasta: flash (.text+.rodata, o `used`
  do Flash) e RAM estatica (.data+.bss, o `used` do RAM). `base` e um perfil onde
  F esta LIGADO; a variante e o MESMO perfil com F desligado, derivada pelo modelo
  de recursos (tools/features.toml -> gen_features.compose), para que um recurso
  com `off_exclude` (bluetooth, som) realmente TIRE o .cpp do build, nao so um -D.

O objetivo do Angelo: garantir economia de recursos AO MAXIMO ao desligar drivers
e qualquer funcionalidade. Nao se garante o que nao se mede: esta ferramenta mede,
e o modo --check TRAVA a economia num piso (marca d'agua, como o orcamento de flash
e o sprawl), so que ao contrario — a economia so pode CRESCER. Se alguem acrescenta
codigo que, ligado a um recurso, deixa de sumir quando o recurso e desligado, a
economia cai e o portao reprova: o recurso vazou para o caminho sempre-ligado.

  tools/measure_savings.py                # tabela: economia atual de cada recurso
  tools/measure_savings.py --only air     # so um recurso (rapido)
  tools/measure_savings.py --update        # grava o piso em tools/feature_savings.json
  tools/measure_savings.py --check         # reprova se a economia caiu abaixo do piso

Metrica: o `used` que o proprio PlatformIO reporta (ELF), nao o .bin — e o numero
consistente entre base e variante, e o que importa aqui e a DIFERENCA. O flash e o
numero travado (deterministico); a RAM entra com tolerancia (ruido de alinhamento
do linker) — ver RAM_TOL.

@project SIMUT — Integrated Universal Monitoring and Telemetry System
@license MIT License
"""
import argparse
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_features as gf  # noqa: E402  (reusa compose/emit_env — mesma fonte do build)

ROOT = gf.ROOT
PROFILES_INI = os.path.join(ROOT, "tools", "generated", "profiles.ini")
LEDGER = os.path.join(ROOT, "tools", "feature_savings.json")

# RAM estatica pode oscilar poucos bytes por alinhamento do linker mesmo quando a
# RAM do recurso nao muda; o piso de RAM aceita essa folga. O flash nao tem folga.
RAM_TOL = 64

# Interruptores do manifesto: (recurso, perfil-base onde esta LIGADO). Desligar =
# virar a chave no dicionario do perfil e recompor — compose() cuida de flag,
# exclusao de fonte e lib_ignore.
MANIFEST_FEATURES = [
    ("cli_full",            "pico_w_test"),
    ("web_https",           "pico_w_release"),
    ("license_stub",        "pico_w_test"),
    ("mdns",                "pico_w_release"),
    ("concurrency_asserts", "pico_w_asserts"),
    ("bluetooth",           "pico_w_alpha"),
    ("air",                 "pico_w_air"),
    ("sound_buzzer",        "pico_w_release"),
]

# Recursos que sao default do simut_config.h (nao vivem no manifesto): desligar =
# acrescentar um -D<macro>=0 aos flags do base. Os sensores sao o caso do Angelo.
FLAG_FEATURES = [
    ("sensor_ds18b20", "pico_w_release", "-DSIMUT_SENSOR_DS18B20=0"),
    ("sensor_dht22",   "pico_w_release", "-DSIMUT_SENSOR_DHT22=0"),
    ("sensor_bme280",  "pico_w_release", "-DSIMUT_SENSOR_BME280=0"),
]

# Recursos que nao sao chaveaveis isolados — o corte depende de outro recurso.
# A auditoria de 2026-09-26 achou sound_buzzer e air acoplados via SoundManager.h
# (no-op so sob SIMUT_AIR); o macro SIMUT_SOUND_BUZZER destravou o buzzer. air
# CONTINUA acoplado, por outro motivo: o perfil air usa display=nenhum, que exclui
# DisplayManager.cpp, e o caminho de boot nao-air (SIMUT_AIR=0) referencia a
# DisplayManager cheia — link quebra. SIMUT_AIR e uma variante de build (headless +
# ciclo dormente), nao uma chave isolada; medir seu custo pediria desemaranhar o
# mostrador. Fica fora do piso.
COUPLED = {
    "air": "SIMUT_AIR=0 com display=nenhum nao linka (DisplayManager.cpp excluida "
           "pelo mostrador headless; o boot nao-air chama a DisplayManager cheia)",
}
# Recursos que economizam quando LIGADOS (desligar ADICIONA bytes). Medidos e
# mostrados, mas fora do piso 'desligar economiza'.
INVERTED = {
    "license_stub": "o stub REMOVE o texto da licenca; desligar o adiciona (~1728 B)",
}

SIZE_RE = {
    "flash": re.compile(r"Flash:.*?used\s+(\d+)\s+bytes", re.I),
    "ram":   re.compile(r"RAM:.*?used\s+(\d+)\s+bytes", re.I),
}


def all_feature_names():
    return [f[0] for f in MANIFEST_FEATURES] + [f[0] for f in FLAG_FEATURES]


def variant_config(feature, base_profile):
    """A config PlatformIO do base com SO `feature` desligado."""
    M = gf.load_manifest()
    prof = gf.resolve_profile(base_profile, M["profiles"])
    for name, base in MANIFEST_FEATURES:
        if name == feature:
            prof[feature] = False
            return gf.compose(prof, M)
    for name, base, off_flag in FLAG_FEATURES:
        if name == feature:
            c = gf.compose(prof, M)
            if off_flag not in c["flags"]:
                c["flags"].append(off_flag)
            return c
    raise KeyError(feature)


def base_profile_of(feature):
    for name, base in MANIFEST_FEATURES:
        if name == feature:
            return base
    for name, base, _ in FLAG_FEATURES:
        if name == feature:
            return base
    raise KeyError(feature)


def parse_sizes(text):
    out = {}
    for key, rx in SIZE_RE.items():
        m = rx.search(text)
        if not m:
            return None
        out[key] = int(m.group(1))
    return out


def pio_build(env):
    """Constroi um env e devolve {'flash':N,'ram':M}, ou None se falhou."""
    pio = os.path.expanduser("~/.platformio/penv/bin/pio")
    if not os.path.exists(pio):
        pio = "pio"
    r = subprocess.run([pio, "run", "-e", env],
                       cwd=ROOT, capture_output=True, text=True)
    sizes = parse_sizes(r.stdout)
    if r.returncode != 0 or sizes is None:
        sys.stderr.write(f"[falha ao construir {env}]\n")
        tail = "\n".join(r.stdout.splitlines()[-8:] + r.stderr.splitlines()[-8:])
        sys.stderr.write(tail + "\n")
        return None
    return sizes


class TempEnvs:
    """Anexa envs de medicao ao profiles.ini gerado e SEMPRE os remove ao sair."""
    def __init__(self, blocks):
        self.blocks = blocks

    def __enter__(self):
        with open(PROFILES_INI, "r", encoding="utf-8") as fh:
            self.original = fh.read()
        marker = "\n; === envs de medicao (measure_savings.py) — temporarios ===\n"
        with open(PROFILES_INI, "w", encoding="utf-8") as fh:
            fh.write(self.original + marker + "\n".join(self.blocks))
        return self

    def __exit__(self, *exc):
        with open(PROFILES_INI, "w", encoding="utf-8") as fh:
            fh.write(self.original)
        return False


def measure(features):
    """Mede a economia de cada recurso. Devolve {recurso: {...}}."""
    # Uma variante-off por recurso; o base reaproveita o env real do perfil.
    variants = {f: (base_profile_of(f), f"_sav_{f}_off") for f in features}
    blocks = [gf.emit_env(env, variant_config(f, base))
              for f, (base, env) in variants.items()]

    results = {}
    base_size_cache = {}
    with TempEnvs(blocks):
        for f in features:
            base, env = variants[f]
            if base not in base_size_cache:
                base_size_cache[base] = pio_build(base)
            bs = base_size_cache[base]
            vs = pio_build(env)
            if bs is None or vs is None:
                results[f] = {"base": base, "error": True}
                continue
            results[f] = {
                "base": base,
                "flash_saved": bs["flash"] - vs["flash"],
                "ram_saved":   bs["ram"] - vs["ram"],
                "flash_base":  bs["flash"],
                "flash_off":   vs["flash"],
            }
    return results


def print_table(results):
    order = sorted(results, key=lambda f: -results[f].get("flash_saved", -1e9))
    print(f"{'recurso':<20} {'base':<16} {'flash -B':>10} {'RAM -B':>8}  nota")
    print("-" * 72)
    for f in order:
        r = results[f]
        note = ""
        if f in INVERTED:
            note = "liga p/ economizar"
        elif f in COUPLED:
            note = "acoplado: " + COUPLED[f].split(";")[0]
        if r.get("error"):
            print(f"{f:<20} {r['base']:<16} {'ERRO':>10} {'':>8}  {note}")
            continue
        print(f"{f:<20} {r['base']:<16} {r['flash_saved']:>10} "
              f"{r['ram_saved']:>8}  {note}")
    skipped = [f for f in COUPLED if f not in results]
    for f in skipped:
        print(f"{f:<20} {base_profile_of(f):<16} {'—':>10} {'':>8}  "
              f"acoplado (fora do piso): {COUPLED[f]}")


def load_ledger():
    if os.path.exists(LEDGER):
        with open(LEDGER, "r", encoding="utf-8") as fh:
            return json.load(fh)
    return {}


def cmd_update(results):
    led = load_ledger()
    for f, r in results.items():
        if r.get("error"):
            print(f"[pulando {f}: build falhou]")
            continue
        if f in COUPLED or f in INVERTED:
            print(f"[pulando {f}: fora do piso 'desligar economiza']")
            continue
        led[f] = {"base": r["base"],
                  "flash_saved": r["flash_saved"],
                  "ram_saved": r["ram_saved"]}
    with open(LEDGER, "w", encoding="utf-8") as fh:
        json.dump(led, fh, indent=2, ensure_ascii=False)
        fh.write("\n")
    print(f"marca d'agua gravada: {os.path.relpath(LEDGER, ROOT)}")


def cmd_check(results, targets):
    """Confere so os `targets` medidos nesta rodada que tem piso no ledger."""
    led = load_ledger()
    if not led:
        sys.stderr.write("sem tools/feature_savings.json — rode --update primeiro\n")
        return 1
    bad = []
    checked = 0
    for f in targets:
        floor = led.get(f)
        if floor is None:
            continue                       # sem piso (acoplado/invertido/novo): nao gate
        checked += 1
        r = results.get(f)
        if not r or r.get("error"):
            bad.append(f"{f}: nao medido (build falhou)")
            continue
        if r["flash_saved"] < floor["flash_saved"]:
            bad.append(f"{f}: flash economiza {r['flash_saved']} B < piso "
                       f"{floor['flash_saved']} B (o recurso vazou p/ o sempre-ligado)")
        if r["ram_saved"] < floor["ram_saved"] - RAM_TOL:
            bad.append(f"{f}: RAM economiza {r['ram_saved']} B < piso "
                       f"{floor['ram_saved']} B (tol {RAM_TOL})")
    if bad:
        sys.stderr.write("check_savings: a economia CAIU (o piso so aceita subir):\n")
        for b in bad:
            sys.stderr.write("  " + b + "\n")
        sys.stderr.write("Se a queda e intencional, rode --update e explique no commit.\n")
        return 1
    print(f"check_savings: {checked} recursos, nenhum abaixo do piso de economia.")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", action="append", metavar="RECURSO",
                    help="mede so este recurso (repetivel)")
    ap.add_argument("--update", action="store_true", help="grava o piso no ledger")
    ap.add_argument("--check", action="store_true", help="reprova se a economia caiu")
    ap.add_argument("--list", action="store_true", help="lista os recursos medidos")
    args = ap.parse_args()

    if args.list:
        for f in all_feature_names():
            print(f, "(base", base_profile_of(f) + ")")
        return 0

    if args.only:
        features = args.only                       # forca (util p/ conferir acoplado pos-fix)
    elif args.check:
        features = list(load_ledger().keys()) or \
            [f for f in all_feature_names() if f not in COUPLED]
    else:
        features = [f for f in all_feature_names() if f not in COUPLED]

    results = measure(features)
    print_table(results)

    if args.update:
        cmd_update(results)
    if args.check:
        return cmd_check(results, features)
    return 0


if __name__ == "__main__":
    sys.exit(main())
