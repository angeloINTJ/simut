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
  tools/measure_savings.py --matrix        # custos POR PRODUTO p/ o configurador
  tools/measure_savings.py --checks        # so as combinacoes de conferencia

--matrix mede, para cada produto publicado, o que muda ao inverter CADA chave a
partir dele, e grava tools/feature_costs.json — a estimativa do configurador
(docs/configurador/). Existe porque custo medido numa base nao vale noutra: o
buzzer devolve 5.016 B no SIMUT e 8.224 B no Alpha, e o Bluetooth, que devolve
140.196 B no Alpha, nem cabe no SIMUT (estoura o slot em 105.964 B; medido
2026-09-26). Inversao que quebra uma regra do manifesto nao compila e fica
anotada; inversao que estoura o flash registra o excesso que o linker da.
Depois das inversoes, compila as combinacoes de CHECKS de verdade: a pagina
soma as diferencas, e e contra estas builds que ela mede o quanto a soma erra.

Metrica: o `used` que o proprio PlatformIO reporta (ELF), nao o .bin — e o numero
consistente entre base e variante, e o que importa aqui e a DIFERENCA. O flash e o
numero travado (deterministico); a RAM entra com tolerancia (ruido de alinhamento
do linker) — ver RAM_TOL.

@project SIMUT — Integrated Universal Monitoring and Telemetry System
@license MIT License
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_features as gf  # noqa: E402  (reusa compose/emit_env — mesma fonte do build)

ROOT = gf.ROOT
PROFILES_INI = os.path.join(ROOT, "tools", "generated", "profiles.ini")
LEDGER = os.path.join(ROOT, "tools", "feature_savings.json")
COSTS_PATH = os.path.join(ROOT, "tools", "feature_costs.json")
OVERFLOW_RE = re.compile(r"region `?FLASH'? overflowed by (\d+) bytes")

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


def pio_run(env):
    pio = os.path.expanduser("~/.platformio/penv/bin/pio")
    if not os.path.exists(pio):
        pio = "pio"
    return subprocess.run([pio, "run", "-e", env],
                          cwd=ROOT, capture_output=True, text=True)


# build_webui_gz.py cai para gzip -9 quando o python do PlatformIO nao tem o
# zopfli, e a imagem sai ~2.888 B maior do que o CI a mede. Uma medida assim nao
# e um custo, e um erro de instrumento: recusada, nunca gravada.
NO_ZOPFLI = "WARN zopfli not installed"


def pio_build_full(env, echo=False):
    """Constroi e devolve {'used','bin','ram','sha256'}; se o linker estoura o
    flash, {'overflow': N} (o excesso que ELE mede); senao {'error': a 1a linha}.
    O sha256 do .bin e o que prova que uma chave nao muda nada num produto.
    `echo` repassa a saida inteira do PlatformIO: no CI e o log da build."""
    r = pio_run(env)
    if echo:
        sys.stdout.write(r.stdout)
        sys.stdout.write(r.stderr)
        sys.stdout.flush()
    if NO_ZOPFLI in r.stdout:
        return {"error": "zopfli ausente no python do PlatformIO: medida recusada"}
    sizes = parse_sizes(r.stdout)
    if r.returncode == 0 and sizes:
        binp = os.path.join(ROOT, ".pio", "build", env, "firmware.bin")
        with open(binp, "rb") as fh:
            digest = hashlib.sha256(fh.read()).hexdigest()
        return {"used": sizes["flash"], "bin": os.path.getsize(binp), "ram": sizes["ram"],
                "sha256": digest}
    out = r.stdout + r.stderr
    m = OVERFLOW_RE.search(out)
    if m:
        return {"overflow": int(m.group(1))}
    first = next((ln.strip() for ln in out.splitlines()
                  if "error:" in ln or "undefined reference" in ln
                  or "multiple definition" in ln), "build falhou")
    return {"error": first[:200]}


def pio_build(env):
    """Constroi um env e devolve {'flash':N,'ram':M}, ou None se falhou."""
    r = pio_run(env)
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


def _git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True,
                          text=True).stdout.strip()


# Combinacoes de conferencia: compiladas de verdade, para a pagina medir o erro
# da soma que ela faz (docs/configurador/logic.js). Escolhidas onde a interacao
# e plausivel — Bluetooth junto de rede e console, sensores junto do buzzer
# (os dois usam a PIO), a CLI completa sem a familia que ela comanda — e um caso
# sem interacao, que tem de dar erro zero. Na primeira medicao (2026-09-26) a
# soma errou de 0 a 4.392 B: por isso o numero e medido, nao escrito na pagina.
CHECKS = [
    ("pico_w_release", {"mdns": False, "sound_buzzer": False, "sensor_bme280": False}),
    ("pico_w_release", {"web_https": False, "license_stub": True, "concurrency_asserts": True}),
    ("pico_w_alpha", {"bluetooth": False, "mdns": True, "web_https": True, "cli_full": True}),
    ("pico_w_alpha", {"sound_buzzer": False, "sensor_dht22": False, "sensor_bme280": False}),
    ("pico_w_alpha", {"cli_full": True, "sensor_ds18b20": False}),
    ("pico_w_air", {"bluetooth": False, "web_https": True, "mdns": True}),
    ("pico_w_air", {"cli_full": False, "concurrency_asserts": True, "sensor_bme280": False}),
    ("pico_w_air", {"sensor_ds18b20": False, "sensor_dht22": False, "sensor_bme280": False}),
]


def measure_checks():
    """Compila CHECKS e devolve [{base, set, used, bin, ram}] (ou overflow/error).
    So o numero REAL: a estimativa e a da pagina, que le estes valores e mede o
    proprio erro — assim a soma existe num lugar so."""
    M = gf.load_manifest()
    blocks, plan = [], []
    for i, (p, changes) in enumerate(CHECKS):
        prof = gf.resolve_profile(p, M["profiles"])
        prof.update(changes)
        bad = gf.rule_violations(gf.config_of(prof), M)
        if bad:
            sys.exit(f"measure_savings: a conferencia {i} quebra a regra {bad[0]['id']}")
        env = f"_check_{i}"
        blocks.append(gf.emit_env(env, gf.compose(prof, M)))
        plan.append((p, changes, env))
    out = []
    with TempEnvs(blocks):
        for p, changes, env in plan:
            r = pio_build_full(env)
            r.pop("sha256", None)
            out.append({"base": p, "set": changes, **r})
            shutil.rmtree(os.path.join(ROOT, ".pio", "build", env), ignore_errors=True)
    return out


def print_checks(doc):
    """Console apenas: a mesma soma da pagina, para quem roda ver o erro ja."""
    for c in doc.get("checks", []):
        base = doc["products"][c["base"]]["base"]
        flips = doc["products"][c["base"]]["flips"]
        est = base["used"] + sum(flips[k]["used"] - base["used"] for k in c["set"] if "used" in flips[k])
        what = ", ".join(f"{k}={'on' if v else 'off'}" for k, v in c["set"].items())
        if "used" in c:
            print(f"  {c['base']:<15} {what}: real {c['used']}, soma {est}, erro {c['used'] - est:+} B")
        else:
            print(f"  {c['base']:<15} {what}: {c}")


def cmd_checks():
    with open(COSTS_PATH, encoding="utf-8") as fh:
        doc = json.load(fh)
    doc["checks"] = measure_checks()
    with open(COSTS_PATH, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, indent=2, ensure_ascii=False)
        fh.write("\n")
    print("conferencias (real contra a soma das inversoes):")
    print_checks(doc)
    return 0


def measure_matrix():
    """Para cada produto publicado: a base, e cada chave invertida a partir dela."""
    M = gf.load_manifest()
    products = [p for p in M["profiles"]
                if gf.resolve_profile(p, M["profiles"]).get("publish")]
    blocks, plan = [], []
    for p in products:
        prof = gf.resolve_profile(p, M["profiles"])
        for t in gf.TOGGLE_ORDER:
            flipped = dict(prof)
            flipped[t] = not bool(prof.get(t, False))
            bad = gf.rule_violations(gf.config_of(flipped), M)
            if bad:
                plan.append((p, t, None, bad[0]["id"]))
                continue
            env = f"_cost_{p}_{t}"
            blocks.append(gf.emit_env(env, gf.compose(flipped, M)))
            plan.append((p, t, env, None))

    result = {"products": {}}
    with TempEnvs(blocks):
        for p in products:
            base = pio_build_full(p)
            if "used" not in base:
                sys.exit(f"measure_savings: a base {p} nao compilou: {base}")
            result["products"][p] = {"base": base, "flips": {}}
        for p, t, env, rule in plan:
            if rule:
                result["products"][p]["flips"][t] = {"rule": rule}
                continue
            r = pio_build_full(env)
            # A mesma imagem, byte a byte: a chave nao tem consumidor neste
            # produto (o texto curto da licenca so existe na tela touch). A pagina
            # mostra isso em vez de um "+0 B" que parece economia.
            if r.pop("sha256", None) == result["products"][p]["base"]["sha256"]:
                r["same"] = True
            result["products"][p]["flips"][t] = r
            # uma pasta de build inteira por inversao: nao guardar dezenas delas
            shutil.rmtree(os.path.join(ROOT, ".pio", "build", env), ignore_errors=True)
    for v in result["products"].values():
        v["base"].pop("sha256", None)
    return result


def cmd_matrix():
    res = measure_matrix()
    doc = {
        "_comment": [
            "Custos POR PRODUTO para o configurador (docs/configurador/). GERADO por",
            "tools/measure_savings.py --matrix; nao edite a mao. Para cada produto",
            "publicado: `base` e a imagem como o manifesto a define, e cada `flips.<chave>`",
            "e a MESMA imagem com so aquela chave invertida — used/bin/ram medidos, ou",
            "`overflow` (o excesso que o linker reporta quando nao cabe no slot), ou",
            "`rule` (a regra do manifesto que proibe a combinacao; nao foi compilada).",
            "`same: true` diz que a imagem saiu identica a base, byte a byte: a chave nao",
            "tem consumidor naquele produto, e a pagina diz isso em vez de mostrar +0 B.",
            "A pagina SOMA as diferencas quando mais de uma chave muda: estimativa, e ela",
            "diz isso. O numero real vem da build (tools/build_custom.py). `checks` sao",
            "combinacoes compiladas de verdade: a pagina compara cada uma com a soma dela",
            "e usa o maior erro como margem de \"perto do limite\".",
        ],
        "measured_at": {
            "date": datetime.date.today().isoformat(),
            "src_commit": _git("log", "-1", "--format=%h", "--", "src"),
            "src_dirty": bool(_git("status", "--porcelain", "--", "src")),
        },
        "products": res["products"],
        "checks": measure_checks(),
    }
    with open(COSTS_PATH, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, indent=2, ensure_ascii=False)
        fh.write("\n")
    errors = 0
    for p, v in res["products"].items():
        b = v["base"]
        print(f"\n{p}: base used={b['used']} bin={b['bin']} ram={b['ram']}")
        for t, r in v["flips"].items():
            if r.get("same"):
                print(f"  {t:<20} a mesma imagem: a chave nao muda nada neste produto")
            elif "used" in r:
                print(f"  {t:<20} flash {r['used'] - b['used']:+8} B  ram {r['ram'] - b['ram']:+7} B")
            elif "overflow" in r:
                print(f"  {t:<20} NAO CABE: estoura o slot em {r['overflow']} B")
            elif "rule" in r:
                print(f"  {t:<20} proibida pela regra {r['rule']}")
            else:
                errors += 1
                print(f"  {t:<20} ERRO: {r['error']}")
    print("\nconferencias (real contra a soma das inversoes):")
    print_checks(doc)
    print(f"\ngravado: {os.path.relpath(COSTS_PATH, ROOT)} "
          "— rode python3 tools/gen_features.py para levar ao model.json")
    if errors:
        # Uma inversao que nao compila e um defeito de chaveamento (ou uma regra
        # que falta no manifesto), nao um custo: a pagina ofereceria uma build
        # que falha. Em 2026-09-26 foram duas, no Air — ambas consertadas no src/.
        print(f"measure_savings: {errors} inversao(oes) nao compilam — conserte ou "
              "declare a regra em tools/features.toml [[rules]]")
        return 1
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", action="append", metavar="RECURSO",
                    help="mede so este recurso (repetivel)")
    ap.add_argument("--update", action="store_true", help="grava o piso no ledger")
    ap.add_argument("--check", action="store_true", help="reprova se a economia caiu")
    ap.add_argument("--list", action="store_true", help="lista os recursos medidos")
    ap.add_argument("--matrix", action="store_true",
                    help="custos por produto -> tools/feature_costs.json")
    ap.add_argument("--checks", action="store_true",
                    help="so as combinacoes de conferencia, no feature_costs.json existente")
    args = ap.parse_args()

    if args.matrix:
        return cmd_matrix()
    if args.checks:
        return cmd_checks()

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
