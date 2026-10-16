"""
PlatformIO post-build guard of the Ângulo's RAM — the strip fill and the strip hash must live in SRAM, and the static RAM of the Ângulo must be its budget's.

First duty. ua::Faixa::fillRect( ) and ua::hashFaixa( ) are the two tight loops of every paint: the clear of a 6,400-pixel strip and the hash of its 3,200 words, twelve
times a frame. Measured on the board on 2026-10-16 with study builds that read XIP_CTR_HIT and XIP_CTR_ACC around each paint, the chart screens run with 6.4
to 8.0% XIP cache misses against 1.0 to 1.6% on the dashboard, and everything in a chart paint was about 2.4 times slower per unit of work, these two loops
included (the clears took 3.6 ms a frame on the chart against 1.5 on the dashboard, margin and hash 8.2 against 3.5 to 4.1). In SRAM (a `.time_critical.*`
section, which the linker script copies to RAM at boot) both measured the same on both screens: clears 1.2 ms, margin and hash 2.8 to 2.9 ms; RAM +368 B.

Nothing in the language keeps them there. The section attribute only places an out-of-line copy, and GCC inlined into a caller in flash a function so marked
in DisplayManager.cpp (core1AlarmIsr), voiding the property, so the build is checked with nm rather than trusted, as tools/check_flash_probe.py checks
the flash-probe shims. This applies to the images that carry the Ângulo (any ua::Pintor symbol); the others have nothing to hold and are skipped.

Second duty (2026-10-16, the review of perf/ui-angulo-2canais). The static RAM of the Ângulo was outside every gate, and the heap pays for each byte of it: the
telemetry batch drops to one entry when the free heap falls under HEAP_RESERVE (TelemetryManager.cpp, 32,768 B with TLS telemetry), and a speed-up that put 112 B
there (a memo of the blends and 12 B of rows in the plot's struct) was a change nothing would have said. So the guard adds up the .data and .bss that the objects
under src/ui/angulo define (nm -S on each object of the build: the ELF cannot say which object a symbol came from) and holds the total to tools/ui_ram_budget.json
exactly, as tools/flash_budget.json holds the flash. Growing past the mark without editing it in the same change fails, and so does shrinking below it: a
budget that only rises stops measuring, so the mark comes down in the change that shrinks. Padding the linker adds between objects is not counted; a symbol is.

Run by hand: python3 tools/check_ui_ram.py firmware.elf [--nm arm-none-eabi-nm] [--budget tools/ui_ram_budget.json]. tools/test_check_ui_ram.py holds the cases, negative controls included.

RP2040 map: XIP flash 0x10000000+, SRAM 0x20000000+.

Project: SIMUT
License: MIT
"""

import json
import os
import subprocess
import sys

try:
    Import("env")  # noqa: F821 -- SCons injects it; absent when the test or the command line imports this file
except NameError:
    env = None

SRAM_BASE = 0x20000000
SRAM_END = 0x20042000
REQUIRED = ("ua::hashFaixa", "ua::Faixa::fillRect")
ORCAMENTO = "ui_ram_budget.json"
PASTA_ANGULO = os.path.join("src", "ui", "angulo")


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


def ram_estatica(listagens):
    """The `nm -S` listings of the Angulo's object files in; the bytes of static RAM they define out: .data and .bss (b, B, d, D), and a weak object (V: an
    inline variable, a COMDAT static) once, whatever the number of objects that define it. Code, read-only data, the undefined and the symbols without a size
    are not RAM."""
    total = 0
    fracos = set()
    for texto in listagens:
        for linha in texto.splitlines():
            partes = linha.split(None, 3)
            if len(partes) != 4:
                continue
            try:
                tamanho = int(partes[1], 16)
            except ValueError:
                continue
            tipo, nome = partes[2], partes[3]
            if tipo in ("b", "B", "d", "D"):
                total += tamanho
            elif tipo == "V" and nome not in fracos:
                fracos.add(nome)
                total += tamanho
    return total


def verificar_ram(usado, objetos, orcamentos, nome_env):
    """The static RAM `usado` (bytes) of `objetos` object files against the budgets read from tools/ui_ram_budget.json ({env: {"budget": bytes}}):
    ('ok' | 'erro', lines to print)."""
    if objetos == 0:
        return "erro", ["[ui-ram] FATAL: no object of the Angulo under src/ui/angulo in this build: the static RAM cannot be measured"]
    marca = (orcamentos or {}).get(nome_env, {}).get("budget")
    if not isinstance(marca, int):
        return "erro", [f"[ui-ram] FATAL: no static-RAM budget for {nome_env} in tools/{ORCAMENTO}: the Angulo's objects define {usado} B; record it there"]
    if usado > marca:
        return "erro", [
            f"[ui-ram] FATAL: the Angulo's static RAM is {usado} B, {usado - marca} B over the budget of {marca} B (tools/{ORCAMENTO}).",
            "[ui-ram] The heap pays for it byte for byte (HEAP_RESERVE, TelemetryManager.cpp). Growing is allowed; doing so without editing the budget in the same",
            "[ui-ram] change is not: record the new mark and say in the commit message what the bytes buy.",
        ]
    if usado < marca:
        return "erro", [
            f"[ui-ram] FATAL: the Angulo's static RAM is {usado} B, {marca - usado} B under the budget of {marca} B (tools/{ORCAMENTO}).",
            f"[ui-ram] The mark only goes down: set \"budget\" to {usado} in the same change, or the budget stops measuring.",
        ]
    return "ok", [f"[ui-ram] OK static RAM of the Angulo: {usado} B in {objetos} objects, on its budget"]


def listar_objetos(pasta):
    """The object files (*.o, *.cpp.o) under `pasta`, sorted."""
    achados = []
    for raiz, _, nomes in os.walk(pasta):
        achados += [os.path.join(raiz, n) for n in nomes if n.endswith(".o")]
    return sorted(achados)


def ler_orcamentos(caminho):
    """(the "envs" table, None) or (None, why not)."""
    try:
        with open(caminho, encoding="utf-8") as f:
            return json.load(f)["envs"], None
    except (OSError, ValueError, KeyError, TypeError) as exc:
        return None, f"[ui-ram] FATAL: cannot read the static-RAM budget {caminho} ({exc})"


def medir_ram(elf, nm, caminho_orcamento, nome_env):
    """The objects of the Angulo sit beside the ELF in the build folder (PlatformIO's layout): ('ok' | 'erro', lines)."""
    objetos = listar_objetos(os.path.join(os.path.dirname(os.path.abspath(elf)), PASTA_ANGULO))
    orcamentos, erro = ler_orcamentos(caminho_orcamento)
    if erro:
        return "erro", [erro]
    listagens = [subprocess.check_output([nm, "-S", "--defined-only", o], text=True) for o in objetos]
    return verificar_ram(ram_estatica(listagens), len(objetos), orcamentos, nome_env)


def _nm_path(cc):
    for a, b in (("gcc", "nm"), ("g++", "nm")):
        if cc.endswith(a):
            return cc[: -len(a)] + b
    return "arm-none-eabi-nm"


def _ler(elf, nm):
    return subprocess.check_output([nm, "-C", elf], text=True)


def check_ui_ram(source, target, env):
    elf = str(target[0])
    nm = _nm_path(env.subst("$CC"))
    try:
        texto = _ler(elf, nm)
    except Exception as exc:
        print(f"[ui-ram] WARNING: could not run nm ({exc}) -- check skipped")
        return
    estado, linhas = verificar(texto)
    if estado != "pulado":   # the images without the Angulo have nothing to say
        print("\n".join(linhas))
        estado_ram, linhas_ram = medir_ram(elf, nm, os.path.join(env.subst("$PROJECT_DIR"), "tools", ORCAMENTO), env.subst("$PIOENV"))
        print("\n".join(linhas_ram))
        if estado_ram == "erro":
            estado = "erro"
    if estado == "erro":
        sys.exit(1)


def main(argv):
    if not argv or argv[0].startswith("-"):
        print(__doc__.split("Run by hand:")[1].split("\n")[0].strip())
        return 2
    nm = argv[argv.index("--nm") + 1] if "--nm" in argv else "arm-none-eabi-nm"
    orcamento = argv[argv.index("--budget") + 1] if "--budget" in argv else os.path.join(os.path.dirname(os.path.abspath(__file__)), ORCAMENTO)
    try:
        texto = _ler(argv[0], nm)
    except Exception as exc:
        print(f"[ui-ram] could not run nm on {argv[0]}: {exc}")
        return 2
    estado, linhas = verificar(texto)
    print("\n".join(linhas))
    if estado != "pulado":
        estado_ram, linhas_ram = medir_ram(argv[0], nm, orcamento, os.path.basename(os.path.dirname(os.path.abspath(argv[0]))))
        print("\n".join(linhas_ram))
        if estado_ram == "erro":
            estado = "erro"
    return 1 if estado == "erro" else 0


if env is not None:
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_ui_ram)
elif __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
