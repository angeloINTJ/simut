"""
WebUI asset compressor — generates gzipped PROGMEM arrays from HTML templates.

Reads WebUI.h, finds all R"raw(...)raw" string blocks, compresses each with
gzip level 9, and outputs WebUI_GZ.h with C-style uint8_t arrays and length
constants for use with safeSend_GZ().

Project: SIMUT
License: MIT
"""

import re
import gzip


INPUT_FILE = "WebUI.h"
OUTPUT_FILE = "WebUI_GZ.h"


def process_file():
    """Parse raw string literals from WebUI.h and produce gzip-compressed PROGMEM arrays."""
    with open(INPUT_FILE, "r", encoding="utf-8") as f:
        content = f.read()

    # Match: static const char NAME[] PROGMEM = R"raw(...)raw";
    pattern = re.compile(
        r'static const char (\w+)\[\] PROGMEM = R"raw\((.*?)\)raw";',
        re.DOTALL
    )
    matches = pattern.findall(content)

    out_lines = [
        "#pragma once",
        "#include <Arduino.h>",
        "namespace WebUI_GZ {",
        ""
    ]

    for name, html_content in matches:
        compressed_data = gzip.compress(html_content.encode("utf-8"), compresslevel=9)
        hex_data = [f"0x{byte:02x}" for byte in compressed_data]

        array_lines = []
        for i in range(0, len(hex_data), 16):
            array_lines.append("    " + ", ".join(hex_data[i:i + 16]))

        array_str = ",\n".join(array_lines)
        length = len(compressed_data)
        gz_name = name + "_GZ"

        out_lines.append(f"// {name}: {len(html_content)} bytes -> {length} bytes")
        out_lines.append(f"static const uint8_t {gz_name}[] PROGMEM = {{\n{array_str}\n}};")
        out_lines.append(f"static const size_t {gz_name}_LEN = {length};\n")

    out_lines.append("}")

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(out_lines))

    print(f"Generated {OUTPUT_FILE} with {len(matches)} compressed arrays.")


if __name__ == "__main__":
    process_file()
