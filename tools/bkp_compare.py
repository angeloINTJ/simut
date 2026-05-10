#!/usr/bin/env python3
"""
bkp_compare.py — comparador in-memory de dois arquivos .bkp do SIMUT.

NÃO extrai arquivos para o disco. Lê os dois .bkp completos em memória, parseia
o formato BKP1 (header + TLV entries) e compara SHA256 do conteúdo de cada
arquivo crítico.

Uso:
    python3 tools/bkp_compare.py <canonical.bkp> <new.bkp> \
        [--ignore system.blog,system.old.blog,t_cursor.bin]

Exit codes:
    0  = todos arquivos críticos batem
    1  = pelo menos 1 mismatch (caminho diff/missing/extra)
    2  = erro de parsing/IO

Saída (uma linha por mismatch, ordem estável):
    DIFF:<path>   — conteúdo diferente
    MISSING:<path> — presente em canonical, ausente em new
    EXTRA:<path>   — presente em new, ausente em canonical
    OK count=N    — última linha; N = total de arquivos críticos comparados

Reuso de parser: importa `verify_backup` do mesmo diretório.
"""

import argparse
import hashlib
import os
import sys

# Permite import do verify_backup.py irmão sem PYTHONPATH
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import verify_backup as vb  # noqa: E402


def load_files(path: str) -> dict:
    """Lê .bkp inteiro em memória, retorna {path: sha256_hex}."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < vb.HEADER_SIZE:
        raise ValueError(f"{path}: too small ({len(data)} B)")
    hdr = vb.parse_header(data[:vb.HEADER_SIZE])
    if hdr["magic"] != vb.MAGIC_LE:
        raise ValueError(f"{path}: bad magic 0x{hdr['magic']:08X}")
    payload = data[vb.HEADER_SIZE : vb.HEADER_SIZE + hdr["payload_size"]]
    if len(payload) != hdr["payload_size"]:
        raise ValueError(
            f"{path}: payload size {len(payload)} != declared {hdr['payload_size']}"
        )
    out = {}
    for name, content in vb.walk_entries(payload):
        out[name] = hashlib.sha256(content).hexdigest()
    return out


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="bkp_compare.py")
    p.add_argument("canonical")
    p.add_argument("new")
    p.add_argument(
        "--ignore",
        default="/system.blog,/system.old.blog,/config/t_cursor.bin",
        help="Comma-separated paths to skip (volatile files)",
    )
    args = p.parse_args(argv)

    ignore = {x.strip() for x in args.ignore.split(",") if x.strip()}

    try:
        canon = load_files(args.canonical)
        new = load_files(args.new)
    except (OSError, ValueError) as e:
        print(f"PARSE_ERROR: {e}", file=sys.stderr)
        return 2

    canon_keys = {k for k in canon if k not in ignore}
    new_keys = {k for k in new if k not in ignore}

    mismatches = []
    for k in sorted(canon_keys):
        if k not in new_keys:
            mismatches.append(f"MISSING:{k}")
        elif canon[k] != new[k]:
            mismatches.append(f"DIFF:{k}")
    for k in sorted(new_keys - canon_keys):
        mismatches.append(f"EXTRA:{k}")

    for line in mismatches:
        print(line)
    print(f"OK count={len(canon_keys)} mismatches={len(mismatches)}")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
