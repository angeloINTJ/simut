"""
PlatformIO post-build guard — the Ângulo's strip fill and strip hash must live in SRAM.

ua::Faixa::fillRect( ) and ua::hashFaixa( ) are the two tight loops of every paint: the clear of a 6,400-pixel strip and the hash of its 3,200 words, twelve
times a frame. Measured on the board on 2026-10-16 with study builds that read XIP_CTR_HIT and XIP_CTR_ACC around each paint, the chart screens run with 6.4
to 8.0% XIP cache misses against 1.0 to 1.6% on the dashboard, and everything in a chart paint was about 2.4 times slower per unit of work, these two loops
included (the clears took 3.6 ms a frame on the chart against 1.5 on the dashboard, margin and hash 8.2 against 3.5 to 4.1). In SRAM (a `.time_critical.*`
section, which the linker script copies to RAM at boot) both measured the same on both screens: clears 1.2 ms, margin and hash 2.8 to 2.9 ms; RAM +368 B.

Nothing in the language keeps them there. The section attribute only places an out-of-line copy, and GCC inlined into a caller in flash a function so marked
in DisplayManager.cpp (core1AlarmIsr), voiding the property, so the build is checked with nm rather than trusted, as tools/check_flash_probe.py checks
the flash-probe shims. This applies to the images that carry the Ângulo (any ua::Pintor symbol); the others have nothing to hold and are skipped.

Run by hand: python3 tools/check_ui_ram.py firmware.elf [--nm arm-none-eabi-nm]. tools/test_check_ui_ram.py holds the cases, negative controls included.

RP2040 map: XIP flash 0x10000000+, SRAM 0x20000000+.

Project: SIMUT
License: MIT
"""

import subprocess
import sys

try:
    Import("env")  # noqa: F821 -- SCons injects it; absent when the test or the command line imports this file
except NameError:
    env = None

SRAM_BASE = 0x20000000
SRAM_END = 0x20042000
REQUIRED = ("ua::hashFaixa", "ua::Faixa::fillRect")


def verificar(nm_texto):
    """The output of `nm -C` in; ('ok' | 'pulado' | 'erro', lines to print) out."""
    enderecos = {}
    angulo = False
    for linha in nm_texto.splitlines():
        partes = linha.split(None, 2)
        if len(partes) != 3:
            continue
        try:
            endereco = int(partes[0], 16)
        except ValueError:
            continue
        nome = partes[2]
        if nome.startswith("ua::Pintor::"):
            angulo = True
        base = nome.split("(", 1)[0]
        if base in REQUIRED:
            enderecos.setdefault(base, []).append(endereco)
    if not angulo:
        return "pulado", ["[ui-ram] no Angulo in this image: nothing to hold"]
    erros = []
    for nome in REQUIRED:
        if nome not in enderecos:
            erros.append(f"[ui-ram] FATAL: {nome} is absent from an image that carries the Angulo")
        for a in enderecos.get(nome, []):
            if not (SRAM_BASE <= a < SRAM_END):
                erros.append(f"[ui-ram] FATAL: {nome} @ 0x{a:08x} is outside SRAM")
    if erros:
        return "erro", erros + [
            "[ui-ram] The strip's fill and hash must run from RAM (P12a): restore the section attribute",
            "[ui-ram] (UA_NA_RAM in Faixa.h, __no_inline_not_in_flash_func in Angulo.cpp) and keep them out of line.",
        ]
    return "ok", [f"[ui-ram] OK {nome} @ 0x{a:08x} (SRAM)" for nome in REQUIRED for a in enderecos[nome]]


def _nm_path(cc):
    for a, b in (("gcc", "nm"), ("g++", "nm")):
        if cc.endswith(a):
            return cc[: -len(a)] + b
    return "arm-none-eabi-nm"


def _ler(elf, nm):
    return subprocess.check_output([nm, "-C", elf], text=True)


def check_ui_ram(source, target, env):
    elf = str(target[0])
    try:
        texto = _ler(elf, _nm_path(env.subst("$CC")))
    except Exception as exc:
        print(f"[ui-ram] WARNING: could not run nm ({exc}) -- check skipped")
        return
    estado, linhas = verificar(texto)
    if estado != "pulado":   # the images without the Angulo have nothing to say
        print("\n".join(linhas))
    if estado == "erro":
        sys.exit(1)


def main(argv):
    if not argv or argv[0].startswith("-"):
        print(__doc__.split("Run by hand:")[1].split("\n")[0].strip())
        return 2
    nm = argv[argv.index("--nm") + 1] if "--nm" in argv else "arm-none-eabi-nm"
    try:
        texto = _ler(argv[0], nm)
    except Exception as exc:
        print(f"[ui-ram] could not run nm on {argv[0]}: {exc}")
        return 2
    estado, linhas = verificar(texto)
    print("\n".join(linhas))
    return 1 if estado == "erro" else 0


if env is not None:
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_ui_ram)
elif __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
