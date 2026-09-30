#!/usr/bin/env python3
"""Testa o "feat" do /api/status: os bits que a imagem diz ter sao as chaves do
configurador com que ela foi compilada.

Rodar: python3 tools/test_feature_bits.py

O src/FeatureBits.h e gerado do tools/features.toml, mas o valor que o firmware
imprime nao vem do manifesto: vem dos macros que o compilador de fato viu. Este
teste junta as duas pontas sem compilar firmware nenhum. Para cada um dos seis
perfis, e para cada chave invertida a partir deles, compoe os flags como o build
compoe (gen_features.compose) e pede ao pre-processador do host o valor de
SIMUT_FEATURE_BITS — o mesmo simut_config.h, os mesmos defaults. Tem de dar a
mascara das chaves ligadas. As chaves sao independentes (cada bit le o seu
macro), entao perfil + inversao unica cobre qualquer combinacao que o
configurador monte; o tft_graph, cujo default segue o mostrador, e coberto pelos
perfis de cada mostrador.

Uma combinacao que quebra regra e pulada: o configurador e o build_custom.py a
recusam antes de compilar, e algumas param num #error do simut_config.h.
"""
import copy
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_features as gf  # noqa: E402

ok = fail = 0


def check(label, cond, detail=""):
    global ok, fail
    if cond:
        ok += 1
    else:
        fail += 1
        print(f"  FALHOU {label}" + (f"\n    {detail}" if detail else ""))


def refused(M):
    """A mensagem com que check_manifest recusa M, ou None se aceita."""
    try:
        gf.check_manifest(M)
        return None
    except SystemExit as e:
        return str(e)


M = gf.load_manifest()

# 1. Todo interruptor tem um bit, e os bits nao se repetem.
bits = {t: M["toggles"][t].get("bit") for t in gf.TOGGLE_ORDER}
check("toda chave tem bit", all(isinstance(b, int) for b in bits.values()), repr(bits))
check("nenhum bit se repete", len(set(bits.values())) == len(bits), repr(bits))
check("todo bit cabe em 32", all(0 <= b < 32 for b in bits.values() if isinstance(b, int)))

m2 = copy.deepcopy(M)
m2["toggles"]["mdns"]["bit"] = M["toggles"]["cli_full"]["bit"]
v = refused(m2)
check("bit repetido e recusado pelo gerador",
      v is not None and "bit" in v and "mdns" in v, repr(v))
m2 = copy.deepcopy(M)
del m2["toggles"]["syslog"]["bit"]
v = refused(m2)
check("chave sem bit e recusada pelo gerador",
      v is not None and "syslog" in v and "bit" in v, repr(v))
m2 = copy.deepcopy(M)
m2["toggles"]["syslog"]["bit"] = 32
v = refused(m2)
check("bit fora de 0..31 e recusado", v is not None and "syslog" in v, repr(v))

# 2. O cabecalho gerado e o do manifesto (o --check do gerador tambem cobra).
header = open(os.path.join(ROOT, "src", "FeatureBits.h"), encoding="utf-8").read()
check("o FeatureBits.h commitado e o que o gerador escreve",
      header == gf.build_feature_header(M))


# 3. O pre-processador do host, com os flags de cada perfil e inversao.
def mask_of(cfg):
    return sum(1 << bits[t] for t in gf.TOGGLE_ORDER if cfg[t])


def host_bits(flags, tmp):
    probe = os.path.join(tmp, "probe.c")
    with open(probe, "w") as fh:
        fh.write('#include "FeatureBits.h"\nFEAT_PROBE SIMUT_FEATURE_BITS\n')
    r = subprocess.run(["cpp", "-P", "-I" + os.path.join(ROOT, "src")] + flags + [probe],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return None, r.stderr.strip().splitlines()[-1:] or ["cpp falhou"]
    line = [ln for ln in r.stdout.splitlines() if ln.startswith("FEAT_PROBE")][0]
    expr = line[len("FEAT_PROBE"):].strip()
    if not re.fullmatch(r"[0-9a-fA-FxXuUlL()|<\s]+", expr):
        return None, [f"expressao inesperada: {expr!r}"]
    return eval(re.sub(r"(?<=[0-9a-fA-F])[uUlL]+", "", expr)), None


if shutil.which("cpp") is None:
    check("cpp do host disponivel", False, "instale o gcc: o teste pre-processa o cabecalho")
else:
    tmp = tempfile.mkdtemp(prefix="feat_bits_")
    cases = 0
    for name in M["profiles"]:
        base = gf.resolve_profile(name, M["profiles"])
        variants = [(name, base)]
        for t in gf.TOGGLE_ORDER:
            flip = dict(base)
            flip[t] = not bool(base.get(t, False))
            variants.append((f"{name} {t}={'on' if flip[t] else 'off'}", flip))
        for label, prof in variants:
            cfg = gf.config_of(prof)
            if gf.rule_violations(cfg, M):
                continue
            flags = gf.compose(prof, M)["flags"]
            got, err = host_bits(flags, tmp)
            cases += 1
            check(f"{label}: SIMUT_FEATURE_BITS = chaves ligadas",
                  err is None and got == mask_of(cfg),
                  f"esperado {mask_of(cfg):#07x}, obtido {got if got is None else hex(got)} {err or ''}")
    shutil.rmtree(tmp, ignore_errors=True)
    check("cobriu os seis perfis e as inversoes validas", cases > 6 * 10, f"{cases} casos")

print(f"{ok} ok, {fail} falha(s)")
sys.exit(1 if fail else 0)
