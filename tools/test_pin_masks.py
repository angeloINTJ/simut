#!/usr/bin/env python3
"""Testa as guardas de compilacao da rede por cabo (SIMUT_NET_LAN) em src/simut_config.h:
quem pode usar qual pino, que radio a imagem tem, e como a imagem se chama.

Rodar: python3 tools/test_pin_masks.py

O SIMUT Wire e o SIMUT num Raspberry Pi Pico comum, sem o radio CYW43, com a rede por
cabo num modulo ENC28J60 (a chave `lan` de tools/features.toml). Uma montagem que nao
pode funcionar tem de parar o BUILD, nao a placa, e e o pre-processador que para: o
simut_config.h compara mascaras de pinos (`#if (A & B) != 0`) e recusa, com um #error que
nomeia os dois donos, o modulo no pino do mostrador ou do buzzer, o buzzer no pino do
mostrador, dois fios do proprio modulo no mesmo pino, o Bluetooth e o Air (os dois rodam
no CYW43) com a LAN, um build Wi-Fi sem o CYW43 e um build de LAN que ainda o tem. O nome da variante (SIMUT_ENV_NAME, o que a OTA
compara e a assinatura amarra) e "wire" com o LCD e "wiretft" com a TFT.

Este teste pede ao pre-processador do host, o mesmo `cpp` do test_feature_bits.py e sem
compilar firmware nenhum, o veredito de cada montagem. As checagens do CYW43 so valem na
toolchain do firmware (ARDUINO_ARCH_RP2040, que toda placa arduino-pico passa): as suites
nativas compilam este cabecalho sem nenhum dos dois macros, e um caso aqui garante que
continuam compilando.

Um teste so vale se morder, e este morde sozinho: para cada #error da secao, tira a
linha de uma copia temporaria do cabecalho e roda tudo de novo -- exatamente UM caso tem
de falhar, o que fixa aquele #error. Um #error novo sem caso reprova o teste.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_features as gf  # noqa: E402

TARGET = "-DARDUINO_ARCH_RP2040"        # toda placa arduino-pico: sem ele o cabecalho e o do host
CYW43 = "-DPICO_CYW43_SUPPORTED=1"      # as placas W passam; o pico_base tambem
# Um build com a chave `lan`: o macro e o que a chave passa junto (on_flags do manifesto),
# hoje os doze slots. main( ) confere que esta lista e a do manifesto.
LAN = ["-DSIMUT_NET_LAN=1", "-DMAX_SENSORS=12"]
LCD = ["-DSIMUT_DISPLAY_TFT=0", "-DSIMUT_DISPLAY_ALPHA=1", "-DHD44780_MODE_PARALLEL=1"]
LCD_I2C = ["-DSIMUT_DISPLAY_TFT=0", "-DSIMUT_DISPLAY_ALPHA=1"]    # o modo I2C e o padrao do LCD
NO_DISPLAY = ["-DSIMUT_DISPLAY_TFT=0", "-DSIMUT_DISPLAY_ALPHA=0"]

# Cada #error da secao 10b do simut_config.h, por um trecho do texto que so ele tem. Cada
# um tem de ter UM caso que o provoque (`error=` abaixo) e uma linha so no cabecalho.
ERR_NO_CYW43 = "needs the CYW43"
ERR_CYW43_LEFT = "still defined"
ERR_BLUETOOTH = "SIMUT_BLUETOOTH cannot be combined"
ERR_AIR = "SIMUT_AIR cannot be combined"
ERR_ENC_DISPLAY = "_INT_PIN) is also a pin of the display"
ERR_ENC_BUZZER = "_INT_PIN) is also BUZZER_PIN"
ERR_BUZZER_DISPLAY = ": BUZZER_PIN is also a pin of the display"
ERR_NO_NAME = "has a variant name"
ERR_ENC_SAME = "are on the same GPIO"
ERR_ENC_SLOT = "is also a sensor slot's"
OWNED = [ERR_NO_CYW43, ERR_CYW43_LEFT, ERR_BLUETOOTH, ERR_AIR, ERR_ENC_DISPLAY, ERR_ENC_BUZZER,
         ERR_BUZZER_DISPLAY, ERR_NO_NAME, ERR_ENC_SAME, ERR_ENC_SLOT]

# (rotulo, flags, veredito). O veredito e ("ok", nome da variante) ou ("erro", trecho).
CASES = [
    # -- os pinos: o INT do modulo segue o mostrador -------------------------------------
    ("LCD paralelo + LAN + INT 21 (o D7 do LCD)", [TARGET] + LAN + LCD + ["-DSIMUT_ENC_INT_PIN=21"], ("erro", ERR_ENC_DISPLAY)),
    ("TFT + LAN + INT 21", [TARGET] + LAN + ["-DSIMUT_ENC_INT_PIN=21"], ("ok", "wiretft")),
    ("LCD paralelo + LAN + INT 28", [TARGET] + LAN + LCD + ["-DSIMUT_ENC_INT_PIN=28"], ("ok", "wire")),
    ("LCD paralelo + LAN, INT pelo padrao (GP28)", [TARGET] + LAN + LCD, ("ok", "wire")),
    ("TFT + LAN, INT pelo padrao (GP21)", [TARGET] + LAN, ("ok", "wiretft")),
    ("LCD em I2C + LAN, INT pelo padrao (GP28)", [TARGET] + LAN + LCD_I2C, ("ok", "wire")),
    ("LCD paralelo + LAN, sem INT (-1): o driver consulta o modulo", [TARGET] + LAN + LCD + ["-DSIMUT_ENC_INT_PIN=-1"], ("ok", "wire")),
    ("LCD paralelo + LAN + INT 12 (o MISO do proprio modulo)", [TARGET] + LAN + LCD + ["-DSIMUT_ENC_INT_PIN=12"], ("erro", ERR_ENC_SAME)),
    # -- os slots: com 16, os GP12-GP15 do modulo seriam slots de sensor -------------------
    ("LCD paralelo + LAN com os 16 slots", [TARGET, LAN[0]] + LCD, ("erro", ERR_ENC_SLOT)),
    # -- o buzzer: GP22 de fabrica, e so conta ligado ------------------------------------
    ("LCD paralelo + LAN + buzzer no GP13 (o CS do modulo)", [TARGET] + LAN + LCD + ["-DBUZZER_PIN=13"], ("erro", ERR_ENC_BUZZER)),
    ("LCD paralelo + LAN + buzzer no GP13, buzzer desligado", [TARGET] + LAN + LCD + ["-DBUZZER_PIN=13", "-DSIMUT_SOUND_BUZZER=0"], ("ok", "wire")),
    ("LCD paralelo + LAN + buzzer no GP20 (o D6 do LCD)", [TARGET] + LAN + LCD + ["-DBUZZER_PIN=20"], ("erro", ERR_BUZZER_DISPLAY)),
    # -- o radio: o Bluetooth e o Air rodam no CYW43 -------------------------------------
    ("LAN + Bluetooth", [TARGET] + LAN + LCD + ["-DSIMUT_BLUETOOTH=1"], ("erro", ERR_BLUETOOTH)),
    ("LAN + Air", [TARGET] + LAN + LCD + ["-DSIMUT_AIR=1"], ("erro", ERR_AIR)),
    ("Wi-Fi sem PICO_CYW43_SUPPORTED, na toolchain do firmware", [TARGET], ("erro", ERR_NO_CYW43)),
    ("Wi-Fi com PICO_CYW43_SUPPORTED", [TARGET, CYW43], ("ok", "release")),
    ("LAN com PICO_CYW43_SUPPORTED", [TARGET, CYW43] + LAN + LCD, ("erro", ERR_CYW43_LEFT)),
    ("LAN sem PICO_CYW43_SUPPORTED", [TARGET] + LAN + LCD, ("ok", "wire")),
    ("Wi-Fi sem PICO_CYW43_SUPPORTED, no host (as suites nativas)", [], ("ok", "release")),
    # -- o nome: o que a OTA compara e a assinatura amarra ---------------------------------
    ("LAN + LCD", LAN + LCD, ("ok", "wire")),
    ("LAN + TFT", list(LAN), ("ok", "wiretft")),
    ("LAN sem mostrador", LAN + NO_DISPLAY, ("erro", ERR_NO_NAME)),
    ("LAN + LCD no RP2350", LAN + ["-DPICO_RP2350=1"] + LCD, ("ok", "wiretwo")),
    ("LAN + TFT no RP2350", LAN + ["-DPICO_RP2350=1"], ("ok", "wiretfttwo")),
]

# Como cada imagem do manifesto se chama. Os nomes das imagens que ja existem nao mudam
# com a chave nova (a OTA do campo os conhece), e um perfil novo precisa de um nome
# decidido aqui antes de existir.
EXPECT_NAME = {
    "pico_w_release": "release", "pico_w_test": "release", "pico_w_test_https": "release",
    "pico_w_asserts": "release", "pico_w_alpha": "alpha", "pico_w_air": "air",
    "pico2_w_release": "releasetwo", "pico_wire": "wire",
}

ERR_LINE = re.compile(r"error: #error")
PROBE = "#include \"simut_config.h\"\nPROBE_ENV SIMUT_ENV_NAME\n"


def preprocess(flags, probe, inc_dirs):
    """O `cpp` do host sobre o cabecalho: (codigo de saida, nome da variante, linhas de #error)."""
    r = subprocess.run(["cpp", "-P"] + [f"-I{d}" for d in inc_dirs] + flags + [probe],
                       capture_output=True, text=True)
    errors = [ln for ln in r.stderr.splitlines() if ERR_LINE.search(ln)]
    line = next((ln for ln in r.stdout.splitlines() if ln.startswith("PROBE_ENV")), "")
    return r.returncode, "".join(re.findall(r'"([^"]*)"', line)), errors


def judge(label, flags, want, probe, inc_dirs):
    """None se o caso passa; senao, o que deu errado."""
    code, name, errors = preprocess(flags, probe, inc_dirs)
    kind, what = want
    if kind == "erro":
        if code == 0 or not errors:
            return f"era para parar num #error ({what!r}) e foi aceito"
        if len(errors) != 1 or what not in errors[0]:
            return f"parou, mas no #error errado ({what!r} esperado): {errors}"
        return None
    if code != 0 or errors:
        return f"era para ser aceito como {what!r} e parou: {errors or code}"
    if name != what:
        return f"aceito, mas com o nome {name!r} em vez de {what!r}"
    if not re.fullmatch(r"[a-z]{1,11}", name):
        return f"o nome {name!r} nao cabe na etiqueta ([a-z], 11 letras: BuildIdentity.cpp, validation.h)"
    return None


def manifest_cases(M):
    """Cada imagem do manifesto, com os flags que o build lhe da -- o compose( ) e o
    CYW43 do pico_base menos o que o perfil tira --, tem de passar, com o nome de sempre."""
    cases = []
    for name in M["profiles"]:
        c = gf.compose(gf.resolve_profile(name, M["profiles"]), M)
        flags = list(c["flags"]) + [TARGET]
        if CYW43 not in c["unflags"]:
            flags.append(CYW43)
        if gf.resolve_profile(name, M["profiles"]).get("chip") == "rp2350":
            flags.append("-DPICO_RP2350=1")
        cases.append((f"imagem {name}", flags, ("ok", EXPECT_NAME.get(name, "?"))))
    return cases


def run_all(cases, probe, inc_dirs):
    return [(label, judge(label, flags, want, probe, inc_dirs)) for label, flags, want in cases]


def main():
    if shutil.which("cpp") is None:
        print("FALHOU cpp do host disponivel\n    instale o gcc: o teste pre-processa o cabecalho")
        return 1
    M = gf.load_manifest()
    fails = []

    # 0. O build LAN deste teste e o que a chave `lan` do manifesto da: o macro e os on_flags.
    lan_manifest = ["-DSIMUT_NET_LAN=1"] + list(M["toggles"]["lan"].get("on_flags", []))
    if LAN != lan_manifest:
        fails.append(f"o LAN do teste {LAN} nao e o que a chave lan do manifesto passa {lan_manifest}")

    # 0. O teste conhece o nome de todo perfil, e nenhum a mais.
    if set(EXPECT_NAME) != set(M["profiles"]):
        fails.append(f"os nomes esperados {sorted(EXPECT_NAME)} nao sao os perfis do manifesto "
                     f"{sorted(M['profiles'])}: decida o nome de OTA do que mudou")

    cases = CASES + manifest_cases(M)
    tmp = tempfile.mkdtemp(prefix="pin_masks_")
    try:
        probe = os.path.join(tmp, "probe.c")
        with open(probe, "w") as fh:
            fh.write(PROBE)

        # 1. O cabecalho como esta.
        baseline_ok = True
        for label, why in run_all(cases, probe, [SRC]):
            if why:
                baseline_ok = False
                fails.append(f"{label}: {why}")

        # 2. Cada #error da secao tem uma linha so e um caso so que o fixa; e todo #error
        #    que fala da LAN ou do CYW43 e um deles.
        header = os.path.join(SRC, "simut_config.h")
        with open(header, encoding="utf-8") as fh:
            lines = fh.read().split("\n")
        error_lines = [i for i, ln in enumerate(lines) if ln.startswith("#error")]
        owned_at = {}
        for token in OWNED:
            hit = [i for i in error_lines if token in lines[i]]
            if len(hit) != 1:
                fails.append(f"o #error de {token!r} esta em {len(hit)} linhas do simut_config.h, esperava 1")
            else:
                owned_at[token] = hit[0]
            pinned = [label for label, _flags, want in cases if want == ("erro", token)]
            if len(pinned) != 1:
                fails.append(f"o #error de {token!r} tem {len(pinned)} casos, esperava 1: {pinned}")
        strangers = [i + 1 for i in error_lines
                     if re.search(r"SIMUT_NET_LAN|CYW43", lines[i]) and i not in owned_at.values()]
        if strangers:
            fails.append(f"#error sobre a LAN ou o CYW43 sem caso, nas linhas {strangers} do simut_config.h")

        # 3. O teste morde: sem um #error, exatamente o caso dele falha. Com o cabecalho ja
        #    reprovado no passo 1 a prova nao diz nada, e so atrapalharia a leitura.
        bitten = 0
        for token, at in (owned_at.items() if baseline_ok else []):
            mutant = os.path.join(tmp, "mutant")
            shutil.rmtree(mutant, ignore_errors=True)
            os.makedirs(mutant)
            with open(os.path.join(mutant, "simut_config.h"), "w", encoding="utf-8") as fh:
                fh.write("\n".join(lines[:at] + [""] + lines[at + 1:]))   # a linha vira vazia: as demais nao andam
            failed = [label for label, why in run_all(cases, probe, [mutant, SRC]) if why]
            want = [label for label, _flags, w in cases if w == ("erro", token)]
            if failed == want:
                bitten += 1
            else:
                fails.append(f"sem o #error de {token!r}, falharam {failed or 'nenhum caso'}; "
                             f"era para falhar so {want}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    if fails:
        print("test_pin_masks: FALHOU")
        for f in fails:
            print("  -", f)
        return 1
    print(f"test_pin_masks: {len(cases)} montagens ({len(CASES)} escolhidas, {len(M['profiles'])} do manifesto) "
          f"julgadas pelo pre-processador; {bitten} de {len(OWNED)} #error mordem (sem a linha, "
          f"exatamente o caso dele falha).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
