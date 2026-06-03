#!/usr/bin/env python3
"""Gera Favicon.cpp com o conteúdo de data/favicon.ico como array PROGMEM.
Roda manualmente quando o favicon mudar:
    .venv/bin/python tools/build_favicon_header.py
"""
from pathlib import Path

ROOT = Path(__file__).parent.parent
SRC = ROOT / "tools" / "favicon-source" / "favicon.ico"
DST = ROOT / "Favicon.cpp"

if not SRC.exists():
    raise SystemExit(f"missing {SRC}")

data = SRC.read_bytes()
lines = []
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")

out = f"""/* Auto-gerado por tools/build_favicon_header.py — NÃO editar à mão.
 * Fonte: data/favicon.ico ({len(data)} bytes)
 * Regenerar:  .venv/bin/python tools/build_favicon_header.py
 */
#include "Favicon.h"

const uint8_t Favicon::DATA[] PROGMEM = {{
{chr(10).join(lines)}
}};

const size_t Favicon::LEN = {len(data)};
"""

DST.write_text(out)
print(f"wrote {DST.relative_to(ROOT)} ({len(data)} bytes embedded)")
