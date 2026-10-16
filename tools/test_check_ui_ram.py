#!/usr/bin/env python3
"""test_check_ui_ram.py -- the cases of tools/check_ui_ram.py, with the negative controls: the guard must fail when the strip's fill or hash is in flash,
absent, or past the end of SRAM, and must not fail an image that does not carry the Angulo. Exit 0 when every case holds."""
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
print(f"{'OK' if not falhas else 'FAILED'}: {casos - falhas} of {casos} cases")
sys.exit(1 if falhas else 0)
