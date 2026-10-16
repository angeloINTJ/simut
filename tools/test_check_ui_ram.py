#!/usr/bin/env python3
"""test_check_ui_ram.py -- the cases of tools/check_ui_ram.py, with the negative controls: the guard must fail when the strip's fill or hash is in flash,
absent, or past the end of SRAM, and must not fail an image that does not carry the Angulo; and the Angulo's static RAM must fail when it is 4 B over or under its
budget. Exit 0 when every case holds."""
import importlib.util
import pathlib
import sys

spec = importlib.util.spec_from_file_location("check_ui_ram", pathlib.Path(__file__).resolve().parent / "check_ui_ram.py")
guarda = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guarda)

HASH = "ua::hashFaixa(unsigned short const*, unsigned int)"
FILL = "ua::Faixa::fillRect(short, short, short, short, unsigned short)"
OUTROS = "10000100 T main\n10001000 T ua::Pintor::repetir()\n10002000 T ua::Faixa::fillScreen(unsigned short)\n20000100 D g_uaVm\n         U memcpy\n"


def nm(hash_=0x20000480, fill=0x20000540, extra=""):
    linhas = OUTROS + extra
    if hash_ is not None:
        linhas += f"{hash_:08x} T {HASH}\n"
    if fill is not None:
        linhas += f"{fill:08x} W {FILL}\n"
    return linhas


falhas = 0
casos = 0


def caso(nome, texto, esperado):
    global falhas, casos
    casos += 1
    estado, linhas = guarda.verificar(texto)
    ok = estado == esperado
    print(f"{'ok  ' if ok else 'FAIL'} {nome}: {estado}" + ("" if ok else f" (expected {esperado}): {linhas}"))
    falhas += not ok


caso("both in SRAM", nm(), "ok")
caso("the hash in flash (the negative control)", nm(hash_=0x10004a20), "erro")
caso("the fill in flash (the negative control)", nm(fill=0x10004b00), "erro")
caso("both in flash", nm(0x10004a20, 0x10004b00), "erro")
caso("the hash absent from an Angulo image", nm(hash_=None), "erro")
caso("the fill absent from an Angulo image", nm(fill=None), "erro")
caso("first byte of SRAM", nm(hash_=0x20000000), "ok")
caso("last byte of SRAM", nm(fill=0x20041ffe), "ok")
caso("just past SRAM", nm(fill=0x20042000), "erro")
caso("a second copy of the fill in flash", nm(extra=f"10005000 W {FILL}\n"), "erro")
caso("a function of the same name prefix does not count", nm(fill=None, extra="10005000 T ua::Faixa::fillRectX(short)\n20000540 W ua::Faixa::fillRect(short)\n"), "ok")
caso("an image without the Angulo, the functions in flash", "10000100 T main\n10004a20 T ua::hashFaixa(unsigned short const*, unsigned int)\n", "pulado")
caso("an empty listing", "", "pulado")
caso("no Angulo and nothing of ours", "10000100 T main\n20000100 D x\n", "pulado")


# ---- the static RAM of the Angulo against tools/ui_ram_budget.json (2026-10-16)
import json
import os
import shutil
import subprocess
import tempfile


def confere(nome, obtido, esperado):
    global falhas, casos
    casos += 1
    ok = obtido == esperado
    print(f"{'ok  ' if ok else 'FAIL'} {nome}: {obtido!r}" + ("" if ok else f" (expected {esperado!r})"))
    falhas += not ok


def listagem(*linhas):
    return "\n".join(linhas) + "\n"


def simbolo(secao, tamanho, nome, ligacao="l", tipo="O"):
    """A line of `objdump -t`: the binding (l local, g global, w weak, u unique), the type (O object, F function), the section, the size, the name."""
    flags = (ligacao if ligacao in "lgu" else " ") + ("w" if ligacao == "w" else " ") + "    " + tipo
    return f"00000000 {flags} {secao}\t{tamanho:08x} {nome}"


# bss 0x60 + 0xc, data 4 + 8 = 120 B of RAM; the rest is not RAM (read-only data, code, an undefined symbol, a section symbol, a symbol with no size)
OBJ_A = listagem("SYMBOL TABLE:", "00000000 l    df *ABS*\t00000000 Angulo.cpp", "00000000 l    d  .bss\t00000000 .bss",
                 simbolo(".bss._ZN2uaL10s_lembradasE", 0x60, "_ZN2uaL10s_lembradasE"), simbolo(".bss._ZN2ua6g_plotE", 0xc, "_ZN2ua6g_plotE", "g"),
                 simbolo(".data.s_dado", 4, "s_dado"), simbolo(".data.g_ini", 8, "g_ini", "g"),
                 simbolo(".rodata.kTabela", 0x20, "kTabela"), simbolo(".rodata._ZN2ua6kGlobalE", 0x10, "kGlobal", "g"),
                 simbolo(".text._ZN2ua5Pintor3xxEv", 0xf0, "_ZN2ua5Pintor3xxEv", "g", "F"), simbolo(".text.local", 0x20, "local", "l", "F"),
                 "00000000 g       *UND*\t00000000 memcpy", simbolo(".bss.sem_tamanho", 0, "sem_tamanho"))
# the review's finding (round 2): a weak read-only table and a vtable are V in nm, as a weak variable is, and sit in flash
FRACOS_FLASH = listagem(simbolo(".rodata._ZL17SENSOR_TYPE_TABLE", 0x110, "SENSOR_TYPE_TABLE", "w"), simbolo(".rodata._ZTV4Base", 0x20, "_ZTV4Base", "w"),
                        simbolo(".data.rel.ro._ZN5Other5tableE", 0, "_ZN5Other5tableE", "w"))
OBJ_B = listagem(simbolo(".data.s_dado", 4, "s_dado"), simbolo(".bss._ZGVZ3fooE1x", 8, "_ZGVZ3fooE1x", "w")) + FRACOS_FLASH
OBJ_C = listagem(simbolo(".bss._ZGVZ3fooE1x", 8, "_ZGVZ3fooE1x", "w"), simbolo(".bss.g_so_do_c", 2, "g_so_do_c", "g")) + FRACOS_FLASH
OBJ_SEM_SECAO_PROPRIA = listagem(simbolo(".bss", 16, "contador", "g"), simbolo(".data", 4, "valor"))
EXIGENCIAS = {"pico_wiretft_angulo": {"budget": 120}}

confere("the .bss and .data symbols add up, the rest is not RAM", guarda.ram_estatica([OBJ_A]), 120)
confere("nothing to add up", guarda.ram_estatica([]), 0)
confere("a weak const table and a vtable are flash, not RAM (the negative control of the review)", guarda.ram_estatica([FRACOS_FLASH]), 0)
confere("a weak variable counts, once for the objects that define it; the weak tables in them do not; two statics of one name are two",
        guarda.ram_estatica([OBJ_B, OBJ_C]), 4 + 8 + 2)
confere("two statics of one name in two objects are two statics", guarda.ram_estatica([listagem(simbolo(".data.s", 4, "s")), listagem(simbolo(".data.s", 4, "s"))]), 8)
confere("a build without -fdata-sections: the symbols of the plain .bss and .data count", guarda.ram_estatica([OBJ_SEM_SECAO_PROPRIA]), 20)
confere("on the budget", guarda.verificar_ram(120, 5, EXIGENCIAS, "pico_wiretft_angulo")[0], "ok")
estado, linhas = guarda.verificar_ram(124, 5, EXIGENCIAS, "pico_wiretft_angulo")
confere("4 B more of static RAM fails (the negative control)", estado, "erro")
confere("... and says by how much and where the budget is", "4 B over" in linhas[0] and "ui_ram_budget.json" in linhas[0], True)
confere("4 B more with the budget edited in the same change", guarda.verificar_ram(124, 5, {"pico_wiretft_angulo": {"budget": 124}}, "pico_wiretft_angulo")[0], "ok")
estado, linhas = guarda.verificar_ram(116, 5, EXIGENCIAS, "pico_wiretft_angulo")
confere("4 B less fails until the mark comes down (a budget that only rises stops measuring)", estado, "erro")
confere("... and says to lower it to 116", "116" in linhas[1] and "goes down" in linhas[1], True)
confere("an image with no budget recorded", guarda.verificar_ram(120, 5, EXIGENCIAS, "pico_wiretft_outro")[0], "erro")
confere("no objects of the Angulo found", guarda.verificar_ram(0, 0, EXIGENCIAS, "pico_wiretft_angulo")[0], "erro")
confere("a budget that is not a number", guarda.verificar_ram(120, 5, {"pico_wiretft_angulo": {"budget": "120"}}, "pico_wiretft_angulo")[0], "erro")

with tempfile.TemporaryDirectory() as pasta:
    p = pathlib.Path(pasta)
    (p / "telas").mkdir()
    for nome in ("Angulo.cpp.o", "telas/GraficosGrafico.cpp.o", "telas/leia-me.txt", "Angulo.cpp.d"):
        (p / nome).write_bytes(b"x")
    confere("the objects are found in the folder and the folders below it, and only they", [os.path.relpath(o, pasta) for o in guarda.listar_objetos(pasta)],
            ["Angulo.cpp.o", os.path.join("telas", "GraficosGrafico.cpp.o")])
    (p / "bom.json").write_text(json.dumps({"envs": {"e": {"budget": 7}}}))
    (p / "ruim.json").write_text("{ not json")
    (p / "sem_envs.json").write_text("{}")
    confere("a budget file is read", guarda.ler_orcamentos(str(p / "bom.json")), ({"e": {"budget": 7}}, None))
    confere("a malformed budget file is an error, not an empty budget", guarda.ler_orcamentos(str(p / "ruim.json"))[0], None)
    confere("a budget file without envs is an error", guarda.ler_orcamentos(str(p / "sem_envs.json"))[0], None)
    confere("a missing budget file is an error", guarda.ler_orcamentos(str(p / "nao_existe.json"))[0], None)

    # the real thing, on a compiler's objects: a static array is RAM, an inline variable is a weak RAM object (RAM), an inline const table is a weak read-only
    # object (flash, and V in nm all the same); N + 4 bytes of RAM fail a budget of N
    if shutil.which("g++") and shutil.which("objdump"):
        def medir(n):
            fonte, objeto = p / f"b{n}.cpp", p / f"b{n}.o"
            fonte.write_text(f"static char buffer[{n}];\nchar *usa( ) {{ return buffer; }}\n"
                             "inline const unsigned tabela[50] = { 1, 2, 3 };\nunsigned le( int i ) { return tabela[i]; }\n"
                             "inline unsigned contador[8];\nunsigned *conta( ) { return contador; }\n")
            subprocess.check_call(["g++", "-std=c++17", "-c", "-fdata-sections", "-o", str(objeto), str(fonte)])
            return guarda.ram_estatica([subprocess.check_output(["objdump", "-t", str(objeto)], text=True)])
        confere("a compiler's object: a 100 B array and a 32 B weak variable are 132 B, the 200 B weak const table is not RAM", medir(100), 132)
        confere("a compiler's object: 4 B more fails a budget of 132", guarda.verificar_ram(medir(104), 1, {"e": {"budget": 132}}, "e")[0], "erro")
        confere("a compiler's object: the budget edited to 136 passes", guarda.verificar_ram(medir(104), 1, {"e": {"budget": 136}}, "e")[0], "ok")
    else:
        print("skip the compiler's objects: no g++ or objdump")

print(f"{'OK' if not falhas else 'FAILED'}: {casos - falhas} of {casos} cases")
sys.exit(1 if falhas else 0)
