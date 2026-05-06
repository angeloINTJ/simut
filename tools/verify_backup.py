#!/usr/bin/env python3
"""
verify_backup.py — Validador standalone de arquivos .bkp do SIMUT (Fase 1 OTA).

Sem dependências externas (stdlib Python 3.7+).

Valida:
  - Magic "BKP1" (0x31504B42)
  - schema_version conhecida
  - header_crc32 (poly 0xEDB88320, init/xor 0xFFFFFFFF, 36 bytes)
  - payload_size bate com bytes restantes do arquivo
  - payload_crc32 bate com o CRC32 do payload integral
  - Walk consistente das entradas TLV (sem overruns)

Uso:
    python3 tools/verify_backup.py <arquivo.bkp>
    python3 tools/verify_backup.py <arquivo.bkp> --list
    python3 tools/verify_backup.py <arquivo.bkp> --extract <dir>

Exit codes:
    0  = válido
    1  = falha de validação ou I/O
    2  = uso incorreto

Layout (src/ota/backup_format.h):
    BackupHeader (40 B):
        u32 magic
        u16 schema_version
        u16 reserved0
        u8  chip_id[8]
        u32 firmware_version  (encoded: (major<<16)|(minor<<8)|patch)
        u32 timestamp         (Unix epoch UTC; 0 se NTP indisponível)
        u32 payload_size
        u32 payload_crc32
        u32 header_crc32
    payload: sequência de TLVs:
        BackupEntry (6 B):
            u16 path_length
            u32 content_length
        + char path[path_length]
        + u8   content[content_length]
"""

import argparse
import os
import struct
import sys
import zlib
from datetime import datetime, timezone

MAGIC_LE = 0x31504B42  # "BKP1" little-endian
# Header: magic(I) schema(H) reserved0(H) chip_id(8s) fwv(I) ts(I)
#         payload_size(I) payload_crc(I) reserved1(I) header_crc(I) → 40 bytes
HEADER_FMT = "<IHH8sIIIIII"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
ENTRY_FMT = "<HI"
ENTRY_SIZE = struct.calcsize(ENTRY_FMT)


def crc32(data: bytes) -> int:
    """CRC32 idêntico ao usado pelo firmware (poly EDB88320, init/xor 0xFFFFFFFF)."""
    return zlib.crc32(data) & 0xFFFFFFFF


def decode_version(v: int) -> str:
    """Decodifica (major<<16)|(minor<<8)|patch em string 'vM.m.p'."""
    return f"v{(v >> 16) & 0xFF}.{(v >> 8) & 0xFF}.{v & 0xFF}"


def fmt_chip_id(chip_id: bytes) -> str:
    return chip_id.hex()


def fmt_ts(ts: int) -> str:
    if ts == 0:
        return "<no NTP>"
    try:
        return datetime.fromtimestamp(ts, tz=timezone.utc).isoformat()
    except (OSError, OverflowError, ValueError):
        return f"<invalid {ts}>"


def parse_header(buf: bytes):
    if len(buf) < HEADER_SIZE:
        raise ValueError(f"file shorter than header ({len(buf)} < {HEADER_SIZE})")
    (magic, schema, _reserved0, chip_id, fwv, ts,
     payload_size, payload_crc, _reserved1, header_crc) = struct.unpack(HEADER_FMT, buf[:HEADER_SIZE])

    if magic != MAGIC_LE:
        raise ValueError(f"bad magic: 0x{magic:08X} (expected 0x{MAGIC_LE:08X} 'BKP1')")
    if schema != 1:
        raise ValueError(f"unsupported schema_version {schema} (this validator knows v1)")

    expected_hcrc = crc32(buf[:HEADER_SIZE - 4])
    if header_crc != expected_hcrc:
        raise ValueError(f"header CRC mismatch: file=0x{header_crc:08X} computed=0x{expected_hcrc:08X}")

    return {
        "magic": magic,
        "schema_version": schema,
        "chip_id": chip_id,
        "firmware_version": fwv,
        "timestamp": ts,
        "payload_size": payload_size,
        "payload_crc32": payload_crc,
        "header_crc32": header_crc,
    }


def walk_entries(payload: bytes):
    """Itera TLVs no payload. Yield (path: str, content: bytes, offset: int)."""
    off = 0
    while off < len(payload):
        if off + ENTRY_SIZE > len(payload):
            raise ValueError(f"truncated entry header at offset {off}")
        path_len, content_len = struct.unpack_from(ENTRY_FMT, payload, off)
        off += ENTRY_SIZE
        if off + path_len > len(payload):
            raise ValueError(f"truncated path (need {path_len} B at offset {off})")
        try:
            path = payload[off:off + path_len].decode("utf-8")
        except UnicodeDecodeError as e:
            raise ValueError(f"path is not valid UTF-8 at offset {off}: {e}")
        off += path_len
        if off + content_len > len(payload):
            raise ValueError(f"truncated content (need {content_len} B at offset {off})")
        content = payload[off:off + content_len]
        off += content_len
        yield path, content


def fmt_size(n: int) -> str:
    if n < 1024:
        return f"{n} B"
    if n < 1024 * 1024:
        return f"{n / 1024:.1f} KB"
    return f"{n / (1024 * 1024):.2f} MB"


def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        prog="verify_backup.py",
        description="Validate a SIMUT .bkp backup file.",
    )
    p.add_argument("file", help="Path to .bkp file")
    p.add_argument("--list", action="store_true", help="Print file table")
    p.add_argument("--extract", metavar="DIR",
                   help="Extract files into DIR (preserving paths)")
    p.add_argument("--quiet", "-q", action="store_true", help="Only print errors")
    args = p.parse_args(argv)

    try:
        with open(args.file, "rb") as f:
            data = f.read()
    except OSError as e:
        print(f"FAIL: cannot read {args.file}: {e}", file=sys.stderr)
        return 1

    if not args.quiet:
        print(f"file: {args.file} ({len(data)} bytes)")

    try:
        h = parse_header(data)
    except ValueError as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1

    payload = data[HEADER_SIZE:]
    if h["payload_size"] != len(payload):
        print(f"FAIL: payload_size mismatch: header={h['payload_size']} actual={len(payload)}",
              file=sys.stderr)
        return 1

    actual_pcrc = crc32(payload)
    if actual_pcrc != h["payload_crc32"]:
        print(f"FAIL: payload CRC mismatch: header=0x{h['payload_crc32']:08X} "
              f"computed=0x{actual_pcrc:08X}", file=sys.stderr)
        return 1

    if not args.quiet:
        print(f"  magic            : 0x{h['magic']:08X} (BKP1)")
        print(f"  schema_version   : {h['schema_version']}")
        print(f"  chip_id          : {fmt_chip_id(h['chip_id'])}")
        print(f"  firmware_version : {decode_version(h['firmware_version'])} (0x{h['firmware_version']:08X})")
        print(f"  timestamp        : {h['timestamp']}  ({fmt_ts(h['timestamp'])})")
        print(f"  payload_size     : {h['payload_size']} B  ({fmt_size(h['payload_size'])})")
        print(f"  payload_crc32    : 0x{h['payload_crc32']:08X}  (matches)")
        print(f"  header_crc32     : 0x{h['header_crc32']:08X}  (matches)")

    files = []
    try:
        for path, content in walk_entries(payload):
            files.append((path, len(content), content))
    except ValueError as e:
        print(f"FAIL: payload walk: {e}", file=sys.stderr)
        return 1

    if not args.quiet:
        print(f"  files            : {len(files)}")

    if args.list:
        print()
        print("Files in backup:")
        for path, sz, _ in files:
            print(f"  {sz:>10}  {path}")

    if args.extract:
        out_dir = args.extract
        os.makedirs(out_dir, exist_ok=True)
        for path, sz, content in files:
            # Strip leading slash to keep extraction inside out_dir
            rel = path.lstrip("/")
            full = os.path.join(out_dir, rel)
            os.makedirs(os.path.dirname(full) or ".", exist_ok=True)
            with open(full, "wb") as f:
                f.write(content)
            if not args.quiet:
                print(f"  extracted {sz:>10}  {rel}")

    if not args.quiet:
        print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
