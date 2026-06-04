#!/usr/bin/env python3
"""
history_v1_to_v2.py — converte arquivos de historico SIMUT v1 (28 B fixos)
para v2 (header + anchor/delta + sensor-mask + zigzag varint).

Uso:
    python3 tools/history_v1_to_v2.py [src_dir] [dst_dir]

Defaults:
    src_dir = data/history
    dst_dir = data/history_v2

Apos executar, mover os arquivos de dst_dir para data/history e fazer
upload via 'pio run -t uploadfs'. NAO ha retro-compatibilidade no
firmware: arquivos v1 serao ignorados pelo reader.

Formato v2 (deve casar bit-a-bit com HistoryCodec.cpp):
  Header (16 B): "SIM2" + version(u16=2) + anchorPeriod(u16=60) +
                 flags(u32=0) + recordCount(u32)
  Records: 1 anchor (28 B raw) a cada 60, 59 deltas variaveis entre.
  Delta:  mask(u16 LE) + zigzag-varint Δepoch + por bit setado da mask
          (bit 0=amb_temp, 1=amb_hum, 2..11=sensors[0..9]) um
          zigzag-varint Δfield. Se fieldHasValid era false, o varint
          carrega o valor ABSOLUTO; senao Δ contra last_valid.
"""

import os
import struct
import sys

V1_RECORD_FMT  = "<I h h 10h"     # epoch, ambT, ambH, sensors[10]
V1_RECORD_SIZE = 28
V2_HEADER_FMT  = "<4sHHII"
V2_HEADER_SIZE = 16
ANCHOR_PERIOD  = 60
NAN_SENTINEL   = -32768

def write_varint_zz(buf: bytearray, v: int):
    """Escreve int como zigzag varint (assina com bit 0)."""
    u = ((v << 1) & 0xFFFFFFFF) ^ ((v >> 31) & 0xFFFFFFFF)
    while u >= 0x80:
        buf.append((u & 0x7F) | 0x80)
        u >>= 7
    buf.append(u & 0x7F)

def encode_delta(rec, last_valid, has_valid):
    """rec, last_valid: tuples (epoch, ambT, ambH, s0..s9). Retorna bytes."""
    buf = bytearray()
    mask = 0
    if rec[1] != NAN_SENTINEL: mask |= (1 << 0)
    if rec[2] != NAN_SENTINEL: mask |= (1 << 1)
    for i in range(10):
        if rec[3 + i] != NAN_SENTINEL:
            mask |= (1 << (2 + i))
    # Bits 12..15 reservados = 0; mask cabe em 12 bits, top byte ≤ 0x0F
    buf.append(mask & 0xFF)
    buf.append((mask >> 8) & 0x0F)

    # Δepoch sempre presente
    write_varint_zz(buf, rec[0] - last_valid[0])

    # amb_temp
    if mask & (1 << 0):
        d = rec[1] - last_valid[1] if has_valid[0] else rec[1]
        write_varint_zz(buf, d)
    if mask & (1 << 1):
        d = rec[2] - last_valid[2] if has_valid[1] else rec[2]
        write_varint_zz(buf, d)
    for i in range(10):
        if mask & (1 << (2 + i)):
            d = rec[3 + i] - last_valid[3 + i] if has_valid[2 + i] else rec[3 + i]
            write_varint_zz(buf, d)
    return bytes(buf)

def convert_file(in_path: str, out_path: str) -> tuple:
    """Retorna (in_size, out_size, num_records)."""
    with open(in_path, "rb") as f:
        data = f.read()

    if len(data) % V1_RECORD_SIZE != 0:
        print(f"  WARN: {in_path} size {len(data)} nao e multiplo de {V1_RECORD_SIZE}; "
              f"truncando os ultimos {len(data) % V1_RECORD_SIZE} bytes")
        data = data[: len(data) - (len(data) % V1_RECORD_SIZE)]

    n_records = len(data) // V1_RECORD_SIZE
    out = bytearray()
    out += struct.pack(V2_HEADER_FMT, b"SIM2", 2, ANCHOR_PERIOD, 0, n_records)

    last_valid = None
    has_valid  = [False] * 12
    counter    = 0

    for i in range(n_records):
        rec_bytes = data[i * V1_RECORD_SIZE : (i + 1) * V1_RECORD_SIZE]
        rec = struct.unpack(V1_RECORD_FMT, rec_bytes)

        if counter == 0:
            # Anchor: 28 B raw
            out += rec_bytes
            last_valid = list(rec)
            has_valid = [
                rec[1] != NAN_SENTINEL,
                rec[2] != NAN_SENTINEL,
            ] + [rec[3 + j] != NAN_SENTINEL for j in range(10)]
        else:
            out += encode_delta(rec, last_valid, has_valid)
            # Atualiza last_valid apenas para campos com mask bit setado
            if rec[1] != NAN_SENTINEL: last_valid[1] = rec[1]; has_valid[0] = True
            if rec[2] != NAN_SENTINEL: last_valid[2] = rec[2]; has_valid[1] = True
            for j in range(10):
                if rec[3 + j] != NAN_SENTINEL:
                    last_valid[3 + j] = rec[3 + j]
                    has_valid[2 + j] = True
            last_valid[0] = rec[0]  # epoch sempre atualiza

        counter = (counter + 1) % ANCHOR_PERIOD

    with open(out_path, "wb") as f:
        f.write(out)

    return (len(data), len(out), n_records)

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "data/history"
    dst = sys.argv[2] if len(sys.argv) > 2 else "data/history_v2"

    if not os.path.isdir(src):
        print(f"ERRO: diretorio fonte nao existe: {src}")
        sys.exit(1)

    os.makedirs(dst, exist_ok=True)

    candidates = sorted(
        fn for fn in os.listdir(src)
        if fn.endswith(".bin") and len(fn) == 12 and fn[:8].isdigit()
    )
    if not candidates:
        print(f"Nenhum arquivo YYYYMMDD.bin encontrado em {src}")
        return

    total_in = total_out = total_rec = 0
    for fn in candidates:
        in_p  = os.path.join(src, fn)
        out_p = os.path.join(dst, fn)
        try:
            sin, sout, n = convert_file(in_p, out_p)
        except Exception as e:
            print(f"  ERRO em {fn}: {e}")
            continue
        ratio = (sout / sin * 100.0) if sin > 0 else 0.0
        saving = sin - sout
        print(f"  {fn}: {sin:>7,} B ({n:>5} recs) -> {sout:>7,} B  "
              f"({ratio:5.1f}%, -{saving:>6,} B)")
        total_in += sin
        total_out += sout
        total_rec += n

    if total_in > 0:
        print()
        print(f"Total: {total_in:,} B -> {total_out:,} B "
              f"({total_out/total_in*100:.1f}%, -{total_in-total_out:,} B = "
              f"{(total_in-total_out)/total_in*100:.1f}% reducao)")
        print(f"Records: {total_rec:,} | Bytes/record medio v1: "
              f"{total_in/total_rec:.1f} | v2: {total_out/total_rec:.2f}")
        print()
        print(f"Arquivos convertidos em {dst}/")
        print("Para fazer upload: mova-os para data/history/ e rode 'pio run -t uploadfs'.")

if __name__ == "__main__":
    main()
