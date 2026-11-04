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
there (a memo of the blends and 12 B of rows in the plot's struct) was a change nothing would have said. So the guard adds up what the objects under src/ui/angulo
put in .data and .bss and holds the total to tools/ui_ram_budget.json exactly, as tools/flash_budget.json holds the flash. Growing past the mark without editing it in
the same change fails, and so does shrinking below it: a budget that only rises stops measuring, so the mark comes down in the change that shrinks.

What counts is decided by SECTION, not by nm's letter. The first version read nm -S and counted every V (weak object) as RAM; nm prints V for a weak read-only table as it
does for a weak variable, and 911 B of them (SENSOR_TYPE_TABLE, channelTable, PinKb::GRIDS, vtables) are flash in the linked ELF, so the mark of 10,224 B held 9,313 B of RAM and
a vtable or a const table that changed would have failed the gate in words about RAM. So the guard reads objdump -t on each object of the build (the ELF cannot say which object a
symbol came from): a symbol of object type, with a size, in a section that the linker script puts in RAM (.bss*, .data*; checked against the linked ELF, where the same names sit
in .bss and .data), counted once if it is weak or unique (a COMDAT variable is one copy in the image, whatever the number of objects that define it). Code that runs from RAM
(.time_critical.*, the two loops of the first duty) is not counted, nor is the padding the linker adds between objects.

Third duty (2026-10-22, S10c C1a). The chip. The end of SRAM is the chip's: the RP2040's is 0x20042000, the RP2350's 0x20082000, so an address that is inside one map is outside the other. The chip
is the image's own, the board's mcu in the build ("build.mcu" of the board the environment names), and by hand `--chip`; it is never read off the addresses, which would make the check agree with
whatever the linker did. An unknown chip on an image that carries the Angulo is an error, not a guess. The two ends come from the core's pico-sdk, rp2040/.../addressmap.h (SRAM_END 0x20042000) and
rp2350/.../addressmap.h (SRAM_END 0x20082000), and the RP2350's agrees with the linker script the Pico 2 W links with, tools/rp2350/memmap_slot.ld (SCRATCH_Y 0x20081000 + 4k); tools/test_check_ui_ram.py
compares the table with the SDK's header when the framework is installed.

The budget of a configurator build. tools/build_custom.py builds in the environment `_custom`, which has no entry in tools/ui_ram_budget.json (so no configurator build of the Angulo linked). It names the
real environment whose entry holds the build, derived from the hardware (chip, network, display), in the project option custom_ui_ram_budget_env of the temporary environment, and this gate reads that entry
instead of one for `_custom`. As a ceiling only: the exact mark, and the rule that it comes down in the change that shrinks, are the repository's for its own images, and a configurator build is no change
to the repository. Over the ceiling fails; under it passes.

Run by hand: python3 tools/check_ui_ram.py firmware.elf --chip rp2040|rp2350 [--nm arm-none-eabi-nm] [--objdump arm-none-eabi-objdump] [--budget tools/ui_ram_budget.json] [--budget-env NAME]. tools/test_check_ui_ram.py holds the cases, negative controls included.

RP2040 map: XIP flash 0x10000000+, SRAM 0x20000000 to 0x20042000. RP2350 map: XIP flash 0x10000000+, SRAM 0x20000000 to 0x20082000 (the 512 KB striped to 0x20080000, then SRAM8 and SRAM9 of 4 KB each).

Project: SIMUT
License: MIT
"""

import json
import os
import re
import subprocess
import sys

try:
    Import("env")  # noqa: F821 -- SCons injects it; absent when the test or the command line imports this file
except NameError:
    env = None

SRAM_BASE = 0x20000000
SRAM_END = {"rp2040": 0x20042000, "rp2350": 0x20082000}   # by chip, the key of [chip.*] in tools/features.toml and the board's mcu; sources in the docstring
OPCAO_ORCAMENTO = "custom_ui_ram_budget_env"   # the project option build_custom.py writes into the temporary environment
REQUIRED = ("ua::hashFaixa", "ua::Faixa::fillRect")
ORCAMENTO = "ui_ram_budget.json"
PASTA_ANGULO = os.path.join("src", "ui", "angulo")


def verificar(nm_texto, chip):
    """The output of `nm -C` in, and the chip the image is built for (it decides where SRAM ends); ('ok' | 'pulado' | 'erro', lines to print) out."""
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
    fim = SRAM_END.get(chip)
    if fim is None:
        return "erro", [f"[ui-ram] FATAL: no SRAM map for the chip {chip!r} (known: {', '.join(SRAM_END)}): where the strip's loops must sit cannot be told.",
                        "[ui-ram] A build passes the board's mcu; by hand, say --chip."]
    erros = []
    for nome in REQUIRED:
        if nome not in enderecos:
            erros.append(f"[ui-ram] FATAL: {nome} is absent from an image that carries the Angulo")
        for a in enderecos.get(nome, []):
            if not (SRAM_BASE <= a < fim):
                erros.append(f"[ui-ram] FATAL: {nome} @ 0x{a:08x} is outside SRAM")
    if erros:
        return "erro", erros + [
            "[ui-ram] The strip's fill and hash must run from RAM (P12a): restore the section attribute",
            "[ui-ram] (UA_NA_RAM in Faixa.h, __no_inline_not_in_flash_func in Angulo.cpp) and keep them out of line.",
        ]
    return "ok", [f"[ui-ram] OK {nome} @ 0x{a:08x} (SRAM)" for nome in REQUIRED for a in enderecos[nome]]


_SIMBOLO = re.compile(r"^[0-9a-fA-F]+ (.{7}) (\S+)\t([0-9a-fA-F]+) (?:\.(?:hidden|protected|internal) )?(\S.*)$")


def _em_ram(secao):
    """The input sections that the linker script puts in RAM: its .bss* and .data* globs. (.rodata*, .text*, .ARM.*, and .time_critical.* are not static data.)"""
    return secao.startswith(".bss") or secao.startswith(".data")


def ram_estatica(listagens):
    """The `objdump -t` listings of the Angulo's object files in; the bytes of static RAM they define out: the symbols of object type, with a size, in a section of
    RAM (_em_ram), a weak or unique one once whatever the number of objects that define it. The section decides, not nm's letter: a weak read-only table is V as a weak
    variable is. Code, read-only data, the undefined, the section symbols and the symbols without a size are not RAM."""
    total = 0
    unicos = set()
    for texto in listagens:
        for linha in texto.splitlines():
            m = _SIMBOLO.match(linha)
            if not m:
                continue
            flags, secao, tamanho, nome = m.group(1), m.group(2), int(m.group(3), 16), m.group(4)
            if flags[6] != "O" or tamanho == 0 or not _em_ram(secao):
                continue
            if flags[0] == "u" or flags[1] == "w":
                if nome in unicos:
                    continue
                unicos.add(nome)
            total += tamanho
    return total


def verificar_ram(usado, objetos, orcamentos, nome_env, so_teto=False):
    """The static RAM `usado` (bytes) of `objetos` object files against the budgets read from tools/ui_ram_budget.json ({env: {"budget": bytes}}):
    ('ok' | 'erro', lines to print). `so_teto`: the entry of `nome_env` is a ceiling for this build and not its exact mark (a configurator build, which
    holds the budget of the real environment it names): under it passes."""
    if objetos == 0:
        return "erro", ["[ui-ram] FATAL: no object of the Angulo under src/ui/angulo in this build: the static RAM cannot be measured"]
    marca = (orcamentos or {}).get(nome_env, {}).get("budget")
    if not isinstance(marca, int):
        return "erro", [f"[ui-ram] FATAL: no static-RAM budget for {nome_env} in tools/{ORCAMENTO}: the Angulo's objects define {usado} B; record it there"]
    if usado > marca and so_teto:
        return "erro", [
            f"[ui-ram] FATAL: the Angulo's static RAM is {usado} B, {usado - marca} B over the budget of {marca} B of {nome_env} (tools/{ORCAMENTO}), which holds this build as a ceiling.",
            "[ui-ram] The heap pays for it byte for byte (HEAP_RESERVE, TelemetryManager.cpp). Something this profile switches grew the Angulo's static RAM past the real image's mark:",
            f"[ui-ram] the change that grows it edits the entry of {nome_env} in the same change.",
        ]
    if usado > marca:
        return "erro", [
            f"[ui-ram] FATAL: the Angulo's static RAM is {usado} B, {usado - marca} B over the budget of {marca} B (tools/{ORCAMENTO}).",
            "[ui-ram] The heap pays for it byte for byte (HEAP_RESERVE, TelemetryManager.cpp). Growing is allowed; doing so without editing the budget in the same",
            "[ui-ram] change is not: record the new mark and say in the commit message what the bytes buy.",
        ]
    if so_teto:
        return "ok", [f"[ui-ram] OK static RAM of the Angulo: {usado} B in {objetos} objects, {marca - usado} B under the ceiling of {marca} B of {nome_env} (this build is held to that entry)"
                      if usado < marca else
                      f"[ui-ram] OK static RAM of the Angulo: {usado} B in {objetos} objects, on the budget of {nome_env} (this build is held to that entry as a ceiling)"]
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


def medir_ram(elf, objdump, caminho_orcamento, nome_env, so_teto=False):
    """The objects of the Angulo sit beside the ELF in the build folder (PlatformIO's layout): ('ok' | 'erro', lines)."""
    objetos = listar_objetos(os.path.join(os.path.dirname(os.path.abspath(elf)), PASTA_ANGULO))
    orcamentos, erro = ler_orcamentos(caminho_orcamento)
    if erro:
        return "erro", [erro]
    listagens = [subprocess.check_output([objdump, "-t", o], text=True) for o in objetos]
    return verificar_ram(ram_estatica(listagens), len(objetos), orcamentos, nome_env, so_teto)


def chip_da_placa(env):
    """The chip the image is built for: the mcu of the board the environment names ("rp2040", "rp2350"), '' when the board does not say."""
    return str(env.BoardConfig().get("build.mcu", "") or "")


def orcamento_do_env(env):
    """(the environment whose entry in tools/ui_ram_budget.json holds this build, whether only as a ceiling): a real environment is held to its own
    entry, exactly; a configurator build names the real environment it is held to in custom_ui_ram_budget_env (tools/build_custom.py), as a ceiling."""
    nomeado = env.GetProjectOption(OPCAO_ORCAMENTO, "")
    if nomeado:
        return nomeado, True
    return env.subst("$PIOENV"), False


def _nm_path(cc):
    for a, b in (("gcc", "nm"), ("g++", "nm")):
        if cc.endswith(a):
            return cc[: -len(a)] + b
    return "arm-none-eabi-nm"


def _objdump_path(cc):
    for a, b in (("gcc", "objdump"), ("g++", "objdump")):
        if cc.endswith(a):
            return cc[: -len(a)] + b
    return "arm-none-eabi-objdump"


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
    estado, linhas = verificar(texto, chip_da_placa(env))
    if estado != "pulado":   # the images without the Angulo have nothing to say
        print("\n".join(linhas))
        nome_env, so_teto = orcamento_do_env(env)
        estado_ram, linhas_ram = medir_ram(elf, _objdump_path(env.subst("$CC")), os.path.join(env.subst("$PROJECT_DIR"), "tools", ORCAMENTO), nome_env, so_teto)
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
    objdump = argv[argv.index("--objdump") + 1] if "--objdump" in argv else "arm-none-eabi-objdump"
    orcamento = argv[argv.index("--budget") + 1] if "--budget" in argv else os.path.join(os.path.dirname(os.path.abspath(__file__)), ORCAMENTO)
    chip = argv[argv.index("--chip") + 1] if "--chip" in argv else None   # no default: the end of SRAM is the chip's, and a guess would agree with any image
    nome_env, so_teto = (argv[argv.index("--budget-env") + 1], True) if "--budget-env" in argv else (os.path.basename(os.path.dirname(os.path.abspath(argv[0]))), False)
    try:
        texto = _ler(argv[0], nm)
    except Exception as exc:
        print(f"[ui-ram] could not run nm on {argv[0]}: {exc}")
        return 2
    estado, linhas = verificar(texto, chip)
    print("\n".join(linhas))
    if estado != "pulado":
        estado_ram, linhas_ram = medir_ram(argv[0], objdump, orcamento, nome_env, so_teto)
        print("\n".join(linhas_ram))
        if estado_ram == "erro":
            estado = "erro"
    return 1 if estado == "erro" else 0


if env is not None:
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_ui_ram)
elif __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
