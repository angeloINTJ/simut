#!/usr/bin/env python3
"""build_custom.py — compila a build montada no configurador, com as regras reaplicadas.

A entrada é um perfil — o produto e o que mudou —, o formato que o configurador
(docs/configurador/) escreve. Em `set`, uma chave é um interruptor do manifesto
(true ou false), `net`, a rede, que leva o id de um driver ([net.*] do manifesto), ou
`ui`, a UI do painel touch, que leva o id de um driver de UI ([ui.*] do manifesto):

    {"v": 1, "base": "pico_w_release", "set": {"net": "enc28j60", "sensor_ds18b20": false}}
    {"v": 1, "base": "pico_wiretft", "set": {"ui": "angulo"}}

A saída, em --out (padrão build-custom/), é o firmware em dois formatos — o
.uf2, que se grava por USB (arrastar para a unidade RPI-RP2), e o .bin, que se
instala pela OTA da interface web — mais build.json com o perfil, os flags, os
tamanhos medidos e os sha256.

A entrada é NÃO CONFIÁVEL: chega por um workflow_dispatch que a página preenche,
e qualquer string que alcançasse o profiles.ini viraria configuração do
PlatformIO. Por isso a validação é estrita e vem antes de tudo: só as chaves do
manifesto, true/false nos interruptores e, em `net` e em `ui`, só o id de um driver que o
manifesto tem, só produtos que existem, tamanho limitado — e tudo o que impede a
build (gen_features.blockers: as regras entre recursos, o driver que o chip não
tem, o orçamento de PIO e de pinos) reaplicado aqui, porque a página não é a
autoridade.

O Ângulo (2026-10-22): o `_custom` não tem entrada no tools/ui_ram_budget.json, e por isso nenhuma build do configurador com o Ângulo linkava (o portão de RAM, tools/check_ui_ram.py,
recusava). Uma build que o leva nomeia o ambiente de verdade cuja entrada a segura, derivado do hardware (chip, rede, mostrador) e do que o orçamento registra (ui_ram_env), na opção
custom_ui_ram_budget_env do ambiente temporário; o portão lê essa entrada, como teto. O número continua o do orçamento: nada é copiado para cá.

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
import check_ui_ram as cur  # noqa: E402  (o portão de RAM do Ângulo: o nome do orçamento e a opção que ele lê)

ROOT = gf.ROOT
ENV = "_custom"
UI_RAM_BUDGET = os.path.join(HERE, cur.ORCAMENTO)   # tools/ui_ram_budget.json: a RAM estática do Ângulo, por ambiente
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
        if key == "net":
            # A rede é uma escolha, não um interruptor: o id de um [net.*] do manifesto, em
            # texto. O tipo vem antes do `in`: um valor em lista ou em objeto não é hashável
            # e daria TypeError, e um bool ou um número nunca é id.
            if not isinstance(value, str):
                raise SpecError(f"net tem de ser o id de um driver de rede ({', '.join(M['net'])}), "
                                f"não {value!r}")
            if value not in M["net"]:
                raise SpecError(f"rede desconhecida: {value!r} (as do manifesto: {', '.join(M['net'])})")
            continue
        if key == "ui":
            # A UI do painel touch é a outra escolha de driver (2026-10-14), e vale o mesmo cuidado: o
            # id de um [ui.*] do manifesto, em texto. O tipo vem antes do `in` (uma lista ou um objeto
            # não é hashável), e o que passasse chegaria a gf.compose( ), que o indexa pelo id.
            if not isinstance(value, str):
                raise SpecError(f"ui tem de ser o id de um driver de UI ({', '.join(M['ui'])}), "
                                f"não {value!r}")
            if value not in M["ui"]:
                raise SpecError(f"UI desconhecida: {value!r} (as do manifesto: {', '.join(M['ui'])})")
            continue
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
    # `changes` traz chaves do TOGGLE_ORDER, `net`, o id do driver de rede, e `ui`, o do driver de UI
    # (parse_spec conferiu os tres): o update poe todas no perfil, onde compose( ) as le.
    prof.update(changes)
    # Uma build do configurador vai para o campo, seja qual for o produto de base:
    # nunca confia na raiz de bancada, nem partindo de uma imagem de bancada
    # (docs/analysis/OTA_ASSINADA.md).
    prof.pop("ota_trust_bench", None)
    return prof


def ui_ram_env(prof, M, budgets=None):
    """(o ambiente de verdade cuja entrada no tools/ui_ram_budget.json segura uma build de `prof` que leva o Ângulo, "") ou (None, por quê).

    O ambiente é o do manifesto que leva o Ângulo no mesmo hardware (chip, rede e mostrador) e tem entrada no orçamento: o pico2_w_angulo para o Pico 2 W, o pico_wiretft_angulo para
    o Wire, seja qual for o produto de que a build parte. Só o nome sai daqui; o número é o da entrada no momento em que o portão o lê (`budgets` é a tabela "envs" do orçamento,
    lida do arquivo quando não vem)."""
    if budgets is None:
        budgets, erro = cur.ler_orcamentos(UI_RAM_BUDGET)
        if budgets is None:
            return None, erro
    hardware = (prof.get("chip") or "rp2040", prof["net"], prof["display"])
    mesmos = []
    for nome in M["profiles"]:
        real = gf.resolve_profile(nome, M["profiles"])
        if real.get("ui", gf.DEFAULT_UI) == "angulo" and (real.get("chip") or "rp2040", real["net"], real["display"]) == hardware:
            mesmos.append(nome)
    com_entrada = [n for n in mesmos if n in budgets]
    if len(com_entrada) == 1:
        return com_entrada[0], ""
    onde = f"chip {hardware[0]}, rede {hardware[1]}, mostrador {hardware[2]}"
    if not com_entrada:
        return None, (f"o Ângulo não tem orçamento de RAM estática para {onde}: nenhum ambiente do manifesto que o leva nesse hardware "
                      f"({', '.join(mesmos) or 'nenhum o leva'}) tem entrada em tools/{cur.ORCAMENTO}")
    return None, f"mais de um ambiente leva o Ângulo para {onde} e tem orçamento ({', '.join(com_entrada)}): qual vale para a build?"


def custom_env(prof, c, ram_env):
    """O texto do [env:_custom] temporário: o que o gerador escreve para qualquer perfil e, numa build que leva o Ângulo, a opção que diz ao portão de RAM de que ambiente de
    verdade vem o orçamento dela (cur.OPCAO_ORCAMENTO)."""
    texto = gf.emit_env(ENV, c)
    if ram_env and prof.get("ui", gf.DEFAULT_UI) == "angulo":
        texto += f"{cur.OPCAO_ORCAMENTO} = {ram_env}\n"
    return texto


def chip_of(base, M):
    """O chip do produto, como o manifesto o diz; sem a chave, o do Pico W."""
    return gf.resolve_profile(base, M["profiles"]).get("chip") or "rp2040"


def outputs_for(base, M):
    """Os arquivos da pasta de build que viram o .bin e o .uf2 entregues.

    No Pico W, uma imagem em dois embrulhos. No Pico 2 W (RP2350), duas imagens,
    as mesmas que a release publica (tools/release_manifest.py): o .bin é a
    atualização marcada para teste, que vai para o outro slot e sobe em teste, e
    o .uf2 é o de fábrica, com a tabela de partições na frente. A tabela vai
    junto para o job de assinatura, que refaz o .uf2 a partir do .bin assinado
    (--finish-signed). tools/check_rp2350_image.py escreve os três depois do
    link."""
    if chip_of(base, M) == "rp2350":
        return {"bin": "firmware_ota.bin", "uf2": "firmware_factory.uf2", "table": "partition_table.uf2"}
    return {"bin": "firmware.bin", "uf2": "firmware.uf2"}


def finish_signed(out_dir):
    """Depois da assinatura: o build.json passa a descrever o .bin assinado e,
    no Pico 2 W, o .uf2 de fábrica é refeito a partir dele, como a release o
    faz, e a tabela sai da pasta. Devolve o código de saída; nada é marcado como
    assinado se faltar o que o .uf2 precisa."""
    import release_manifest  # o mesmo .uf2 de fábrica da release
    path = os.path.join(out_dir, "build.json")
    with open(path, encoding="utf-8") as fh:
        report = json.load(fh)
    bin_path = os.path.join(out_dir, report["files"]["bin"]["file"])
    with open(bin_path, "rb") as fh:
        data = fh.read()
    if report.get("chip") == "rp2350":
        table = os.path.join(out_dir, "partition_table.uf2")
        try:
            uf2 = release_manifest.rp2350_factory(data, table)
        except ValueError as e:
            say(f"o .uf2 de fábrica não saiu — {e}", error=True)
            return 1
        uf2_path = os.path.join(out_dir, report["files"]["uf2"]["file"])
        with open(uf2_path, "wb") as fh:
            fh.write(uf2)
        os.remove(table)
        report["files"]["uf2"].update(sha256=hashlib.sha256(uf2).hexdigest(), bytes=len(uf2))
    report["files"]["bin"].update(sha256=hashlib.sha256(data).hexdigest(), bytes=len(data))
    report["signed"] = True
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2, ensure_ascii=False)
        fh.write("\n")
    say(f"assinado — .bin {len(data)} B" + (", .uf2 de fábrica refeito dele" if report.get("chip") == "rp2350" else ""))
    return 0


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
    ap.add_argument("--finish-signed", metavar="PASTA",
                    help="depois da assinatura (job sign do build-custom.yml): o build.json "
                         "descreve o .bin assinado, e o .uf2 do Pico 2 W é refeito dele")
    args = ap.parse_args()
    if args.finish_signed:
        return finish_signed(args.finish_signed)

    M = gf.load_manifest()
    text = args.profile if args.profile is not None else os.environ.get("SIMUT_PROFILE", "")
    try:
        base, changes = parse_spec(text, M)
    except SpecError as e:
        say(f"perfil recusado — {e}", error=True)
        return 2
    prof = resolve(base, changes, M)
    # Tudo o que impede a build, na ordem da pagina (gen_features.blockers): as regras;
    # o driver de rede sem placa para o chip (o ENC28J60 no RP2350: net_refusal), que o
    # gerador recusaria em compose( ) com um sys.exit; e, desde 2026-10-13, o orcamento
    # de PIO e de pinos (tools/pio_budget.py), sem o qual uma build que compila subia com
    # um sensor sem programa. E perfil invalido (exit 2), e --validate-only ja o diz.
    bad = gf.blockers(prof, M)
    if bad:
        for b in bad:
            if b["kind"] == "rule":
                say(f"perfil recusado — regra {b['id']}: {b['why']['en']}", error=True)
            elif b["kind"] == "unsupported":
                say(f"perfil recusado — {b['detail']}", error=True)
            else:
                say(f"perfil recusado — {b['id']}: {b['why']['en']} ({b['detail']})", error=True)
        return 2
    # O orçamento de RAM do Ângulo (2026-10-22): sem um ambiente de verdade com entrada para este hardware, o portão recusaria no fim do link, depois de minutos.
    ram_env = None
    if prof.get("ui", gf.DEFAULT_UI) == "angulo":
        ram_env, why = ui_ram_env(prof, M)
        if ram_env is None:
            say(f"perfil recusado — {why}", error=True)
            return 2
    spec = canonical(base, changes)
    tag = hashlib.sha256(spec.encode()).hexdigest()[:8]
    for h in gf.hazard_hits(gf.config_of(prof), M):
        say(f"aviso {h['id']}: {h['why']['en']}")
    if args.validate_only:
        say(f"perfil válido — {spec}")
        return 0

    c = gf.compose(prof, M)
    with ms.TempEnvs([custom_env(prof, c, ram_env)]):
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
    outs = outputs_for(base, M)
    chip = chip_of(base, M)
    if chip == "rp2350":
        # A atualização tem de sair marcada para teste: sem a marca, ela
        # instalaria sem teste, e uma ruim não voltaria (OTA_AB_RP2350.md).
        sys.path.insert(0, os.path.join(HERE, "rp2350"))
        import picobin  # noqa: E402  (tools/rp2350/picobin.py)
        with open(os.path.join(build_dir, outs["bin"]), "rb") as fh:
            _, items = picobin.first_block(fh.read())
        if not (items and (items.get(picobin.ITEM_IMAGE_TYPE, 0) >> 16) & picobin.TBYB):
            say(f"{outs['bin']} não saiu marcado para teste", error=True)
            gh_output(result="error")
            return 1
        shutil.copyfile(os.path.join(build_dir, outs["table"]), os.path.join(args.out, "partition_table.uf2"))
    files = {}
    for ext in ("uf2", "bin"):
        dst = os.path.join(args.out, f"{name}.{ext}")
        shutil.copyfile(os.path.join(build_dir, outs[ext]), dst)
        files[ext] = {"file": os.path.basename(dst), "sha256": sha256(dst),
                      "bytes": os.path.getsize(dst)}
    import check_flash_budget  # os mesmos tetos que o portão de flash usa
    ceil = check_flash_budget.ceilings(base)
    ota_max = ceil["ota_bin"]   # o maior .bin que ainda cabe assinado
    fits_ota = ota_max is None or res["bin"] <= ota_max
    report = {
        "profile": json.loads(spec),
        "chip": chip,
        "config": gf.config_of(prof),
        "flags": c["flags"],
        "excludes": c["excludes"],
        "sizes": {"flash_used": res["used"], "bin": res["bin"], "ram_used": res["ram"]},
        "ceilings": ceil,
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
