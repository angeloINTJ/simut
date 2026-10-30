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
  tools/measure_savings.py --matrix --only tel_mqtt   # so as inversoes desta chave
  tools/measure_savings.py --matrix --only net        # so as trocas de rede (net=<id>)
  tools/measure_savings.py --checks        # so as combinacoes de conferencia

--matrix mede, para cada produto publicado, o que muda ao inverter CADA chave a
partir dele, e grava tools/feature_costs.json — a estimativa do configurador
(docs/configurador/). Existe porque custo medido numa base nao vale noutra: o
buzzer devolve 5.016 B no SIMUT e 8.224 B no Alpha, e o Bluetooth, que devolve
140.196 B no Alpha, nem cabe no SIMUT (estoura o slot em 105.964 B; medido
2026-09-26). Inversao que quebra uma regra do manifesto nao compila e fica
anotada; inversao que estoura o flash registra o excesso que o linker da.
A rede e uma escolha de driver, nao um interruptor (2026-10-13): alem das chaves,
a matriz troca o driver de cada produto por cada outro [net.<id>] do manifesto, e
a chave da inversao e `net=<id>` (um texto com "=", que nenhum interruptor tem).
Depois das inversoes, compila as combinacoes de CHECKS de verdade: a pagina
soma as diferencas, e e contra estas builds que ela mede o quanto a soma erra.

Metrica: o `used` que o proprio PlatformIO reporta (ELF), nao o .bin — e o numero
consistente entre base e variante, e o que importa aqui e a DIFERENCA. A RAM entra com
tolerancia (ruido de alinhamento do linker) — ver RAM_TOL. O piso (--check, --update) mede
o flash SEM o enchimento do linker, todo `*fill*` das secoes .boot2, .text, .rodata e .data do
mapa — ver flash_fill( ) — e deixa passar uma queda de FLASH_TOL bytes, o que sobra de
mudanca de verdade no codigo. A --matrix mede o `used` inteiro, porque ali o custo e o
que a imagem ocupa, e o enchimento ocupa o slot (e tools/feature_costs.json guarda isso).

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
# RAM do recurso nao muda; o piso de RAM aceita essa folga. O flash tem a propria (FLASH_TOL).
RAM_TOL = 64

# O piso de flash deixa passar uma queda de FLASH_TOL bytes. Tirado todo o enchimento do linker, o
# que sobra e mudanca de verdade no codigo que a chave guarda ou toca, e anda pouco. Medido em
# 2026-10-29, em seis commits da main (6adf1023..229d5d59): oito dos nove recursos do #296 nao se
# mexeram; o tft_graph perdeu 4 B (a chave tirava 104 B de AppManager::core0Yield( ), 100 B no #294,
# 104 B de novo no #295). Os outros sete, em tres commits, andaram 12 B (cli_full, +12 no #294), 8 B
# (sound_buzzer, -4 no #294 e +8 de la ate o #300) e 1 B (web_history): e esse residuo que 8 B cobrem. A
# menor queda que o piso ja registrou foi de 24 B (sound_buzzer, #234, uma mudanca de codigo, medida em
# 2026-10-22 com o enchimento ainda dentro: docs/analysis/ECONOMIA_DE_RECURSOS.md); o #234 sozinho tira 38 B
# da economia do buzzer (4.999 -> 4.961 B, 2026-10-30, sem o enchimento), bem acima dos 8 B. Com o enchimento
# dentro, a mesma conta andou 8-16 B por commit sem simbolo nenhum mudar de lugar, e ficou abaixo do piso em
# nove de dezesseis recursos.
FLASH_TOL = 8

# O flash do piso e o `used` sem o enchimento do linker. Entre duas secoes de entrada o linker poe um
# `*fill*` para alinhar a seguinte, e a imagem e empurrada ate secoes alinhadas a 8 (a `.fini.__stub` de
# 688 B no .text, as tabelas `time_t[]` atras dos pools de strings no .rodata): o enchimento antes delas
# anda 0-7 B com o tamanho de TUDO que vem antes, e a base e a variante, de cadeias diferentes, andam
# diferente. Medido em 2026-10-29 (#296): a economia de nove recursos andou 8-16 B por commit com removed,
# added e merged identicos, so os `*fill*` mexendo, e a troca de um caractere da versao ("2.12.0" por
# "2.12.1", nenhum codigo) subiu seis delas 8 B. O maior desses enchimentos e o do banco do BTstack
# (__bluetooth_tlv, 8 KB de .rodata que o framework alinha a um setor de 4 KB, em toda imagem com
# Bluetooth: a pico_w_alpha e a pico_w_air): o linker enche ate 4.095 B na frente dele, e em 2026-10-22
# (#285) a economia do bluetooth tinha caido 2.912 B em tres semanas enquanto o codigo que a chave tira
# crescia 8 B, e a do tel_tls tinha subido 4.096 B entre duas arvores com os mesmos simbolos removidos
# (docs/analysis/ECONOMIA_DE_RECURSOS.md). O piso desconta todos: o do banco e so o maior.
# $BUILD_DIR e do SCons: o mapa sai ao lado do firmware.elf de cada ambiente. Muda o
# checksum do projeto, como os ambientes temporarios ja mudam (AGENTS.md).
MAP_FLAG = "-Wl,-Map,$BUILD_DIR/firmware.map"
# As secoes de saida que o PlatformIO soma no `used` do flash: .boot2 + .text + .rodata + .data do
# `size -A` do ELF, sem .ota nem .partition (ficam entre elas no flash e o `used` nao os conta).
# Conferido em 2026-10-29 na pico_w_release de 229d5d59: 1.023.860 B = 256 + 601.520 + 414.408 + 7.676.
# Outro chip soma outras secoes, e flash_fill( ) recusa em vez de ler um enchimento que nao e o do `used`.
FLASH_SECTIONS = (".boot2", ".text", ".rodata", ".data")
_MAP_HEADER_RE = re.compile(r"^(\.\S+)(?:\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+))?")
_MAP_FILL_RE = re.compile(r"^ \*fill\*\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)")
_MAP_ADDR_RE = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)")


def flash_fill(map_text, used):
    """Bytes de `*fill*` que o linker poe dentro das secoes que o `used` soma (FLASH_SECTIONS), lidos do
    mapa. ValueError, nunca 0, se o mapa nao fecha com o `used`.

    So conta a parte "Linker script and memory map": o que vem antes ("Discarded input sections") nao
    ocupa flash. Uma linha sem recuo e o cabecalho de uma secao de saida e os `*fill*` de baixo dela sao
    dela; os de uma secao fora do `used` (.ota, .partition, .bss ...) nao entram. As strings mescladas
    (.str1.1) dividem enderecos e a soma das entradas da .rodata passa do cabecalho, entao o fecho que se
    confere e o dos cabecalhos com o `used`, nao o das entradas. Recusa: o texto que nao e um mapa; o
    mapa sem uma das quatro secoes ou com uma duas vezes; os cabecalhos que nao somam o `used` (outro
    chip, outra conta do PlatformIO: o enchimento lido nao seria o do `used`); um `*fill*` fora de
    qualquer secao, com uma linha que este leitor nao entende, maior que a propria secao ou fora dela.
    Um enchimento que ninguem leu nao e um enchimento de zero."""
    start = map_text.find("Linker script and memory map")
    if start < 0:
        raise ValueError("mapa do linker sem 'Linker script and memory map'")
    lines = map_text[start:].splitlines()
    sections = {}                      # secao do `used` -> (endereco, tamanho) do cabecalho
    fills = {name: [] for name in FLASH_SECTIONS}      # (endereco, tamanho) de cada *fill* dela
    cur = None                         # a secao de saida de que as linhas de baixo fazem parte
    seen = read = 0                    # linhas `*fill*` do mapa / as que este leitor entendeu
    for i, ln in enumerate(lines):
        if ln and not ln[0].isspace():
            m = _MAP_HEADER_RE.match(ln)
            cur = m.group(1) if m else None
            if cur in FLASH_SECTIONS:
                addr, size = m.group(2), m.group(3)
                if addr is None and i + 1 < len(lines):   # nome longo: os numeros vem na linha de baixo
                    m2 = _MAP_ADDR_RE.match(lines[i + 1])
                    if m2:
                        addr, size = m2.group(1), m2.group(2)
                if addr is None:
                    raise ValueError(f"a secao {cur} do mapa sem endereco e tamanho")
                if cur in sections:
                    raise ValueError(f"a secao {cur} aparece duas vezes no mapa")
                sections[cur] = (int(addr, 16), int(size, 16))
        elif ln.lstrip().startswith("*fill*"):
            seen += 1
            m = _MAP_FILL_RE.match(ln)
            if m:
                read += 1
                if cur is None:
                    raise ValueError("um `*fill*` fora de qualquer secao de saida")
                if cur in FLASH_SECTIONS:
                    fills[cur].append((int(m.group(1), 16), int(m.group(2), 16)))
    if seen != read:
        raise ValueError(f"{seen - read} linha(s) `*fill*` que este leitor nao entende")
    missing = [name for name in FLASH_SECTIONS if name not in sections]
    if missing:
        raise ValueError(f"o mapa nao tem a secao {', '.join(missing)}")
    total = sum(size for _, size in sections.values())
    if total != used:
        raise ValueError(f"as secoes {' + '.join(FLASH_SECTIONS)} do mapa somam {total} B e o `used` e {used} B")
    out = 0
    for name in FLASH_SECTIONS:
        lo, size = sections[name]
        for addr, sz in fills[name]:
            if addr < lo or addr + sz > lo + size:
                raise ValueError(f"um `*fill*` de {sz} B em {addr:#x} fora da secao {name} ({lo:#x} + {size:#x})")
            out += sz
    return out


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
    # O Air e a base porque e onde a chave rende tudo: sem o servidor HTTPS, o
    # cliente da telemetria e o unico que liga o motor TLS do BearSSL. No SIMUT o
    # servidor segura a maior parte dele (a matriz mede os quatro produtos).
    ("tel_tls",             "pico_w_air"),
    ("tel_mqtt",            "pico_w_air"),
    ("web_history",         "pico_w_air"),
    ("web_export_api",      "pico_w_release"),
    ("web_metrics",         "pico_w_release"),
    ("syslog",              "pico_w_release"),
    ("tft_graph",           "pico_w_release"),
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


def pio_run(env, linker_map=False):
    """`linker_map`: pede o mapa do linker (MAP_FLAG) somado a qualquer
    PLATFORMIO_BUILD_FLAGS que ja esteja no ambiente."""
    pio = os.path.expanduser("~/.platformio/penv/bin/pio")
    if not os.path.exists(pio):
        pio = "pio"
    run_env = None
    if linker_map:
        run_env = dict(os.environ)
        run_env["PLATFORMIO_BUILD_FLAGS"] = \
            (run_env.get("PLATFORMIO_BUILD_FLAGS", "") + " " + MAP_FLAG).strip()
    return subprocess.run([pio, "run", "-e", env],
                          cwd=ROOT, capture_output=True, text=True, env=run_env)


# build_webui_gz.py cai para gzip -9 quando o python do PlatformIO nao tem o
# zopfli, e a imagem sai ~2.888 B maior do que o CI a mede. Uma medida assim nao
# e um custo, e um erro de instrumento: recusada, nunca gravada.
NO_ZOPFLI = "WARN zopfli not installed"


def pio_build_full(env, echo=False):
    """Constroi e devolve {'used','bin','ram','sha256'}; se o linker estoura o
    flash, {'overflow': N} (o excesso que ELE mede); senao {'error': a 1a linha}.
    O `used` e o inteiro, com o enchimento do linker: e o que a --matrix grava em
    tools/feature_costs.json, porque o custo de uma chave e o que a imagem ocupa no slot.
    O piso mede outra coisa, de proposito (pio_build, gate_flash: sem o enchimento).
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
    """Constroi um env com o mapa do linker e devolve {'flash':N,'ram':M,'fill':F},
    ou None se falhou. F e o enchimento do linker nas secoes do `used` (flash_fill).
    Recusa, como a pio_build_full, a build sem zopfli; e recusa a build sem mapa e a
    de mapa que nao fecha com o `used`, porque um enchimento que nao foi lido nao e
    um enchimento zero."""
    r = pio_run(env, linker_map=True)
    sizes = parse_sizes(r.stdout)
    if r.returncode != 0 or sizes is None:
        sys.stderr.write(f"[falha ao construir {env}]\n")
        tail = "\n".join(r.stdout.splitlines()[-8:] + r.stderr.splitlines()[-8:])
        sys.stderr.write(tail + "\n")
        return None
    if NO_ZOPFLI in r.stdout:
        sys.stderr.write(f"[{env}: zopfli ausente no python do PlatformIO: medida recusada]\n")
        return None
    map_path = os.path.join(ROOT, ".pio", "build", env, "firmware.map")
    if not os.path.exists(map_path):
        sys.stderr.write(f"[{env}: a build nao deixou {map_path}: medida recusada]\n")
        return None
    with open(map_path, encoding="utf-8", errors="replace") as fh:
        try:
            sizes["fill"] = flash_fill(fh.read(), sizes["flash"])
        except ValueError as e:
            sys.stderr.write(f"[{env}: {e}: medida recusada]\n")
            return None
    return sizes


def gate_flash(sizes):
    """O flash que o piso compara: o `used` sem o enchimento do linker (flash_fill). A --matrix nao
    passa por aqui: mede o `used` inteiro (pio_build_full), porque o custo de uma chave e o que a imagem
    ocupa no slot, enchimento incluido."""
    return sizes["flash"] - sizes["fill"]


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
                "flash_saved": gate_flash(bs) - gate_flash(vs),
                "ram_saved":   bs["ram"] - vs["ram"],
                "flash_base":  bs["flash"],
                "flash_off":   vs["flash"],
                "fill_base":   bs["fill"],
                "fill_off":    vs["fill"],
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
        if r.get("fill_base") or r.get("fill_off"):
            note = (note + "; " if note else "") + \
                f"sem o enchimento do linker (base {r['fill_base']} B, off {r['fill_off']} B)"
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
        if r["flash_saved"] < floor["flash_saved"] - FLASH_TOL:
            bad.append(f"{f}: flash economiza {r['flash_saved']} B < piso "
                       f"{floor['flash_saved']} B, tolerancia {FLASH_TOL} B (o recurso vazou p/ o sempre-ligado)")
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
    # As chaves de 2026-09-30, juntas: as duas da telemetria dividem o
    # TelemetryManager, e as quatro da web dividem o WebManager_Core.cpp e a
    # pagina de configuracao — interacao plausivel nos dois casos.
    ("pico_w_air", {"tel_tls": False, "tel_mqtt": False}),
    ("pico_w_air", {"web_history": False, "web_export_api": False, "web_metrics": False,
                    "syslog": False}),
    # O Pico 2 W (2026-10-05): a mesma primeira conferencia do SIMUT, e o
    # Bluetooth com a linha de comando completa, que no slot dele cabem.
    ("pico2_w_release", {"mdns": False, "sound_buzzer": False, "sensor_bme280": False}),
    ("pico2_w_release", {"bluetooth": True, "cli_full": True}),
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
    """Console apenas: a mesma soma da pagina (docs/configurador/logic.js,
    flipDelta), para quem roda ver o erro ja. Uma inversao que estourou conta
    pela extensao que o linker mediu, a regiao mais o estouro, contra o .bin da
    base; ate 2026-10-12 ela ficava de fora daqui e a conferencia do Air com
    HTTPS e mDNS saia com +39.744 B de um erro que nao existia."""
    import check_flash_budget  # os mesmos tetos que a pagina usa
    for c in doc.get("checks", []):
        base = doc["products"][c["base"]]["base"]
        flips = doc["products"][c["base"]]["flips"]
        region = check_flash_budget.ceilings(c["base"])["flash_region"]

        def delta(f):
            if "used" in f:
                return f["used"] - base["used"]
            if "overflow" in f:
                return region + f["overflow"] - base["bin"]
            return 0
        est = base["used"] + sum(delta(flips[flip_key(k, v)]) for k, v in c["set"].items())
        what = ", ".join(flip_key(k, v) if k == "net" else f"{k}={'on' if v else 'off'}"
                         for k, v in c["set"].items())
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


def flip_key(key, value):
    """A chave de `flips` (tools/feature_costs.json) de uma mudanca de `set`, a de um perfil ou a
    de CHECKS: um interruptor e o proprio nome, e a rede e `net=<id>`, um texto com "=" que
    nenhum interruptor tem, entao os dois nunca colidem."""
    return f"{key}={value}" if key == "net" else key


def cost_env(product, key):
    """O ambiente temporario que mede uma inversao. O PlatformIO so aceita letras, digitos, "_" e
    "-" num nome de ambiente (ENVNAME_RE de platformio/project/config.py, conferido na fonte em
    2026-10-13), e `validate( )` olha TODOS os [env:*] a cada `pio run`: um "=" no nome
    derrubaria a medicao inteira, nao so a dele. A chave da rede, `net=<id>`, tem um."""
    return f"_cost_{product}_{key}".replace("=", "_")


def flips_of(prof, M, names=None):
    """As mudancas simples que a matriz mede a partir de um produto: [(chave em `flips`, o perfil
    com ela)]. Um interruptor vira ao contrario; a rede vira para cada [net.<id>] do manifesto
    que nao e o do produto (flip_key: `net=<id>`). `names`: so estas (interruptores e/ou "net"),
    na ordem dada; sem elas, todas, na ordem da pagina."""
    if names is None:
        names = [*gf.TOGGLE_ORDER, "net"]
    out = []
    for name in names:
        if name == "net":
            out += [(flip_key("net", n), dict(prof, net=n)) for n in M["net"] if n != prof["net"]]
        else:
            out.append((name, dict(prof, **{name: not bool(prof.get(name, False))})))
    return out


def plan_flip(blocks, plan, p, key, flipped, M):
    """Poe uma inversao no plano, (produto, chave, ambiente, impedimento), e, quando ha o que
    medir, o ambiente nos blocos. O que impede a build (gen_features.blockers) nao tem imagem a
    medir, e a pagina o trata como sem medida: uma regra fica pelo id; o resto -- o driver de
    rede sem placa para o chip do produto (o ENC28J60 no RP2350) e o orcamento de PIO e de pinos
    (2026-10-13) -- vira `unsupported`, com o motivo. Vale para a troca de rede como para um
    interruptor."""
    bad = gf.blockers(flipped, M)
    if bad and bad[0]["kind"] == "rule":
        plan.append((p, key, None, bad[0]["id"]))
    elif bad:
        plan.append((p, key, None, {"unsupported": bad[0]["detail"]}))
    else:
        env = cost_env(p, key)
        blocks.append(gf.emit_env(env, gf.compose(flipped, M)))
        plan.append((p, key, env, None))


def measure_matrix():
    """Para cada produto publicado: a base, e cada mudanca simples a partir dela (flips_of)."""
    M = gf.load_manifest()
    products = [p for p in M["profiles"]
                if gf.resolve_profile(p, M["profiles"]).get("publish")]
    blocks, plan = [], []
    for p in products:
        prof = gf.resolve_profile(p, M["profiles"])
        for key, flipped in flips_of(prof, M):
            plan_flip(blocks, plan, p, key, flipped, M)

    result = {"products": {}}
    with TempEnvs(blocks):
        for p in products:
            base = pio_build_full(p)
            if "used" not in base:
                sys.exit(f"measure_savings: a base {p} nao compilou: {base}")
            result["products"][p] = {"base": base, "flips": {}}
        for p, key, env, rule in plan:
            if rule:
                result["products"][p]["flips"][key] = rule if isinstance(rule, dict) else {"rule": rule}
                continue
            r = pio_build_full(env)
            # A mesma imagem, byte a byte: a chave nao tem consumidor neste
            # produto (o texto curto da licenca so existe na tela touch). A pagina
            # mostra isso em vez de um "+0 B" que parece economia.
            if r.pop("sha256", None) == result["products"][p]["base"]["sha256"]:
                r["same"] = True
            result["products"][p]["flips"][key] = r
            # uma pasta de build inteira por inversao: nao guardar dezenas delas
            shutil.rmtree(os.path.join(ROOT, ".pio", "build", env), ignore_errors=True)
    for v in result["products"].values():
        v["base"].pop("sha256", None)
    return result


def cmd_matrix_partial(toggles):
    """So as inversoes de `toggles`, gravadas no feature_costs.json que existe.
    Cada nome e uma chave do TOGGLE_ORDER ou "net", que vale por toda troca de
    rede de cada produto (as chaves `net=<id>` de flips_of).

    Vale quando a chave nova nasce desligavel sem mudar nada ligada: as bases
    e as outras inversoes continuam as mesmas imagens. Isso nao se supoe — as
    bases sao reconstruidas e comparadas com o que o arquivo guarda (used,
    bin, ram); qualquer diferenca recusa, e a matriz inteira e o caminho. Custa
    uma build de base e uma por chave em cada produto publicado (quatro desde
    que o Pico 2 W entrou, 2026-10-05), contra 69 e mais 12 conferencias da
    matriz inteira; `net` custa uma por driver que nao e o do produto."""
    M = gf.load_manifest()
    unknown = [t for t in toggles if t != "net" and t not in gf.TOGGLE_ORDER]
    if unknown:
        sys.exit(f"measure_savings: chave desconhecida: {', '.join(unknown)}")
    with open(COSTS_PATH, encoding="utf-8") as fh:
        doc = json.load(fh)
    products = list(doc["products"])
    blocks, plan = [], []
    for p in products:
        prof = gf.resolve_profile(p, M["profiles"])
        for key, flipped in flips_of(prof, M, toggles):
            plan_flip(blocks, plan, p, key, flipped, M)
    with TempEnvs(blocks):
        bases = {}
        for p in products:
            b = pio_build_full(p)
            if "used" not in b:
                sys.exit(f"measure_savings: a base {p} nao compilou: {b}")
            stored = doc["products"][p]["base"]
            if any(b[k] != stored[k] for k in ("used", "bin", "ram")):
                sys.exit(f"measure_savings: a base {p} mudou (hoje used={b['used']} bin={b['bin']} "
                         f"ram={b['ram']}, o arquivo diz {stored}). As outras inversoes podem ter "
                         "mudado junto: rode --matrix inteiro.")
            bases[p] = b
        for p, key, env, rule in plan:
            if rule:
                doc["products"][p]["flips"][key] = rule if isinstance(rule, dict) else {"rule": rule}
                continue
            r = pio_build_full(env)
            if r.pop("sha256", None) == bases[p]["sha256"]:
                r["same"] = True
            doc["products"][p]["flips"][key] = r
            shutil.rmtree(os.path.join(ROOT, ".pio", "build", env), ignore_errors=True)
    doc["measured_at"] = {
        "date": datetime.date.today().isoformat(),
        "src_commit": _git("log", "-1", "--format=%h", "--", "src"),
        "src_dirty": bool(_git("status", "--porcelain", "--", "src")),
        "partial": sorted(set(doc["measured_at"].get("partial", [])) | set(toggles)),
    }
    with open(COSTS_PATH, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, indent=2, ensure_ascii=False)
        fh.write("\n")
    errors = 0
    for p, key, _env, _rule in plan:   # a ordem do plano: por produto, e por chave de inversao
        b = doc["products"][p]["base"]
        r = doc["products"][p]["flips"][key]
        if r.get("same"):
            print(f"{p:<15} {key:<12} a mesma imagem")
        elif "used" in r:
            print(f"{p:<15} {key:<12} flash {r['used'] - b['used']:+8} B  ram {r['ram'] - b['ram']:+7} B")
        elif "rule" in r:
            print(f"{p:<15} {key:<12} proibida pela regra {r['rule']}")
        elif "unsupported" in r:
            print(f"{p:<15} {key:<12} nao se aplica: {r['unsupported']}")
        elif "overflow" in r:
            print(f"{p:<15} {key:<12} NAO CABE: estoura o slot em {r['overflow']} B")
        else:
            errors += 1
            print(f"{p:<15} {key:<12} ERRO: {r['error']}")
    print(f"gravado: {os.path.relpath(COSTS_PATH, ROOT)} — rode python3 tools/gen_features.py")
    return 1 if errors else 0


def cmd_matrix():
    res = measure_matrix()
    doc = {
        "_comment": [
            "Custos POR PRODUTO para o configurador (docs/configurador/). GERADO por",
            "tools/measure_savings.py --matrix; nao edite a mao. Para cada produto",
            "publicado: `base` e a imagem como o manifesto a define, e cada `flips.<chave>`",
            "e a MESMA imagem com so aquela chave invertida — used/bin/ram medidos, ou",
            "`overflow` (o excesso que o linker reporta quando nao cabe no slot), ou",
            "`rule` (a regra do manifesto que proibe a combinacao; nao foi compilada), ou",
            "`unsupported` (o driver de rede pede uma placa que o chip do produto nao tem,",
            "ou a combinacao nao cabe nos blocos de PIO ou nos pinos: nao ha imagem a",
            "medir, e a pagina a conta como sem medida).",
            "`flips.net=<id>` e a mesma coisa para a rede, que e uma escolha de driver e",
            "nao um interruptor: a MESMA imagem com o driver de rede trocado para <id>,",
            "uma entrada por driver que nao e o do produto, medida ou anotada do mesmo",
            "jeito.",
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
            elif "unsupported" in r:
                print(f"  {t:<20} nao se aplica: {r['unsupported']}")
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
        return cmd_matrix_partial(args.only) if args.only else cmd_matrix()
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
