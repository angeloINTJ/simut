"""
WebUI asset compressor — generates gzipped PROGMEM arrays from HTML templates.

Reads WebUI.h, finds all R"raw(...)raw" string blocks, compresses
 * each with gzip level 9, and outputs WebUI_GZ.h with C-style
 * uint8_t arrays and length constants for use with safeSend_GZ().

Project: SIMUT
License: MIT
"""

import re
import gzip
import binascii

INPUT_FILE = "WebUI.h"
OUTPUT_FILE = "WebUI_GZ.h"

# ============================================================================
# LANGUAGE SELECTION — edit here to include optional languages in the build.
# ============================================================================
# Default firmware ships with English (en) and Portuguese Brazil (pt) only.
# Extra languages inflate the compressed WebUI_GZ.h embedded in the firmware
# binary; on a ~1 MB program partition this is significant.
#
# ALL 8 languages remain maintained in WebUI.h (blocks wrapped with
# "// @LANG_BEGIN:xx" / "// @LANG_END:xx" markers) so when new strings are
# added the translator must update every language. Only languages listed in
# ENABLED_LANGS below are emitted into WebUI_GZ.h at compressor time.
#
# To enable a language: add its code to ENABLED_LANGS and re-run this script.
# Available optional codes: es (Spanish), de (German), fr (French),
#                           it (Italian), ru (Russian), zh (Chinese).
ENABLED_LANGS = {"en", "pt"}
# ENABLED_LANGS.add("es")   # Spanish
# ENABLED_LANGS.add("de")   # German
# ENABLED_LANGS.add("fr")   # French
# ENABLED_LANGS.add("it")   # Italian
# ENABLED_LANGS.add("ru")   # Russian
# ENABLED_LANGS.add("zh")   # Chinese
# ============================================================================


_LANG_BEGIN = re.compile(r'^\s*(?://|<!--)\s*@LANG_BEGIN:(\w+)\s*(?:-->)?\s*$')
_LANG_END   = re.compile(r'^\s*(?://|<!--)\s*@LANG_END:(\w+)\s*(?:-->)?\s*$')

def _strip_disabled_langs(content: str) -> str:
    """Remove blocos de idiomas nao habilitados em ENABLED_LANGS.

    Processamento por linha entre pares @LANG_BEGIN:xx / @LANG_END:xx.
    Linhas que estao dentro de um bloco desabilitado sao descartadas junto
    com os proprios marcadores. Nao mexe no resto do arquivo.
    """
    out = []
    skip_depth = 0
    for line in content.splitlines(keepends=True):
        mb = _LANG_BEGIN.match(line)
        me = _LANG_END.match(line)
        if mb:
            lang = mb.group(1)
            if lang not in ENABLED_LANGS:
                skip_depth += 1
                continue
            # Idioma habilitado — remove so o marcador, mantem conteudo.
            continue
        if me:
            lang = me.group(1)
            if skip_depth > 0:
                skip_depth -= 1
            continue
        if skip_depth > 0:
            continue
        out.append(line)
    return "".join(out)


def process_file():
    with open(INPUT_FILE, "r", encoding="utf-8") as f:
        content = f.read()

    # Opt-in de idiomas: strip dos blocos nao habilitados ANTES da compressao.
    content = _strip_disabled_langs(content)

    pattern = re.compile(r'static const char (\w+)\[\] PROGMEM = R"raw\((.*?)\)raw";', re.DOTALL)
    matches = pattern.findall(content)

    out_lines = [
        "#pragma once",
        "#include <Arduino.h>",
        "namespace WebUI_GZ {",
        ""
    ]

    for name, html_content in matches:

        compressed_data = gzip.compress(html_content.encode('utf-8'), compresslevel=9)


        hex_data = [f"0x{byte:02x}" for byte in compressed_data]


        array_lines = []
        for i in range(0, len(hex_data), 16):
            array_lines.append("    " + ", ".join(hex_data[i:i+16]))

        array_str = ",\n".join(array_lines)
        length = len(compressed_data)

        gz_name = name + "_GZ"
        out_lines.append(f"// {name}: {len(html_content)} bytes -> {length} bytes")
        out_lines.append(f"static const uint8_t {gz_name}[] PROGMEM = {{\n{array_str}\n}};")
        out_lines.append(f"static const size_t {gz_name}_LEN = {length};\n")

    out_lines.append("}")

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(out_lines))

    print(f"Ficheiro {OUTPUT_FILE} gerado com sucesso com {len(matches)} arrays comprimidos.")

if __name__ == "__main__":
    process_file()
