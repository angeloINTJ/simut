#!/usr/bin/env python3
"""
generate_history_v2.py — gera arquivos de histórico SIMUT V2 sintéticos para
testes de estresse de telemetria + CSV export.

Formato: bit-a-bit compatível com `HistoryCodec.cpp` (encoder) e Python
reference em `tools/history_v1_to_v2.py` (V2 encoder).

Cada arquivo `YYYYMMDD.bin` contém:
  - 16 B header: "SIM2" + version(u16=2) + anchorPeriod(u16=60) + flags(u32=0)
                 + recordCount(u32)
  - N records: 1 anchor (28 B raw) a cada 60, 59 deltas variáveis entre.

Dados sintéticos:
  - Temp ambiente: 18-30°C (senoidal diária + ruído ±0.5°C)
  - Hum ambiente:  40-80% (senoidal diária + ruído ±2%)
  - Sensors[0..9]: 18-32°C (senoidal + offset por slot + ruído)
  - Records realistas (pequena variação entre samples → boa compressão delta)

Uso:
    python3 generate_history_v2.py [--days N] [--records-per-day M]
                                    [--start-date YYYY-MM-DD]
                                    [--output-dir DIR]
                                    [--max-bytes BYTES]

Defaults:
    --days 30
    --records-per-day 1440  (1/min)
    --start-date <hoje - days>
    --output-dir /tmp/stress_history
    --max-bytes 850000  (deixa margem em FS de 1MB)
"""

import argparse
import math
import os
import random
import struct
import sys
from datetime import datetime, timedelta

# ===== Constantes V2 (devem casar com HistoryCodec.h e history_v1_to_v2.py) =====
V2_HEADER_FMT  = "<4sHHII"
V2_HEADER_SIZE = 16
V1_RECORD_FMT  = "<I h h 10h"      # epoch, ambT, ambH, sensors[10] — usado no anchor
V1_RECORD_SIZE = 28
ANCHOR_PERIOD  = 60
NAN_SENTINEL   = -32768

# ===== Encoding =====

def write_varint_zz(buf: bytearray, v: int):
    u = ((v << 1) & 0xFFFFFFFF) ^ ((v >> 31) & 0xFFFFFFFF)
    while u >= 0x80:
        buf.append((u & 0x7F) | 0x80)
        u >>= 7
    buf.append(u & 0x7F)

def encode_delta(rec, last_valid, has_valid):
    buf = bytearray()
    mask = 0
    if rec[1] != NAN_SENTINEL: mask |= (1 << 0)
    if rec[2] != NAN_SENTINEL: mask |= (1 << 1)
    for i in range(10):
        if rec[3 + i] != NAN_SENTINEL:
            mask |= (1 << (2 + i))
    buf.append(mask & 0xFF)
    buf.append((mask >> 8) & 0x0F)
    write_varint_zz(buf, rec[0] - last_valid[0])
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

# ===== Synthetic data =====

def synth_record(epoch: int, day_offset_seconds: float, slot_phases: list) -> tuple:
    """Gera um record realista. Retorna (epoch, ambT_i16, ambH_i16, *sensors_i16)."""
    # Diurnal: 1 ciclo por dia. Pico ~14h (50400s), mínimo ~05h (18000s).
    cycle = day_offset_seconds / 86400.0
    diurnal = math.sin(2 * math.pi * (cycle - 0.21))  # offset to peak ~14h

    # Ambiente
    amb_t = 24.0 + 5.0 * diurnal + random.uniform(-0.3, 0.3)
    amb_h = 60.0 - 15.0 * diurnal + random.uniform(-1.0, 1.0)

    # Sensors: temp por slot com pequeno offset + fase própria
    sensors = []
    for s in range(10):
        offset, phase = slot_phases[s]
        sensor_t = 25.0 + offset + 4.0 * math.sin(2 * math.pi * (cycle - 0.21 + phase)) + random.uniform(-0.2, 0.2)
        sensors.append(sensor_t)

    # Convert to int16 (×100, NaN se valor absurdo)
    def f2i(v):
        scaled = round(v * 100)
        if scaled > 32767: return 32767
        if scaled < -32767: return -32767
        return scaled

    return (epoch, f2i(amb_t), f2i(amb_h)) + tuple(f2i(t) for t in sensors)

def gen_day_file(out_path: str, day_epoch_start: int, n_records: int, interval_sec: int) -> tuple:
    """Gera um arquivo V2 para um dia. Retorna (file_size, n_records)."""
    # Cada slot tem offset/fase fixos (deterministic per day for repeatability)
    rng = random.Random(day_epoch_start)
    slot_phases = [(rng.uniform(-1.5, 1.5), rng.uniform(-0.05, 0.05)) for _ in range(10)]

    out = bytearray()
    out += struct.pack(V2_HEADER_FMT, b"SIM2", 2, ANCHOR_PERIOD, 0, n_records)

    last_valid = None
    has_valid = [False] * 12
    counter = 0

    for i in range(n_records):
        epoch = day_epoch_start + i * interval_sec
        day_off = i * interval_sec
        rec = synth_record(epoch, day_off, slot_phases)

        if counter == 0:
            # Anchor: 28 B raw
            out += struct.pack(V1_RECORD_FMT, *rec)
            last_valid = list(rec)
            has_valid = [
                rec[1] != NAN_SENTINEL,
                rec[2] != NAN_SENTINEL,
            ] + [rec[3 + j] != NAN_SENTINEL for j in range(10)]
        else:
            out += encode_delta(rec, last_valid, has_valid)
            if rec[1] != NAN_SENTINEL: last_valid[1] = rec[1]; has_valid[0] = True
            if rec[2] != NAN_SENTINEL: last_valid[2] = rec[2]; has_valid[1] = True
            for j in range(10):
                if rec[3 + j] != NAN_SENTINEL:
                    last_valid[3 + j] = rec[3 + j]
                    has_valid[2 + j] = True
            last_valid[0] = rec[0]

        counter = (counter + 1) % ANCHOR_PERIOD

    with open(out_path, "wb") as f:
        f.write(out)
    return len(out), n_records

# ===== Main =====

def main():
    ap = argparse.ArgumentParser(description="Gera arquivos V2 de stress test.")
    ap.add_argument("--days", type=int, default=30, help="Numero de dias (default: 30)")
    ap.add_argument("--records-per-day", type=int, default=1440, help="Records/dia (default: 1440 = 1/min)")
    ap.add_argument("--start-date", default=None, help="YYYY-MM-DD (default: hoje - days)")
    ap.add_argument("--output-dir", default="/tmp/stress_history")
    ap.add_argument("--max-bytes", type=int, default=850000, help="Cap total de bytes gerados (default 850KB)")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if args.start_date:
        start = datetime.strptime(args.start_date, "%Y-%m-%d")
    else:
        start = datetime.now() - timedelta(days=args.days)

    interval_sec = max(1, 86400 // args.records_per_day)
    actual_records = 86400 // interval_sec

    os.makedirs(args.output_dir, exist_ok=True)

    print(f"[gen] Gerando {args.days} dias × {actual_records} records (interval={interval_sec}s) em {args.output_dir}/")
    print(f"[gen] Período: {start.strftime('%Y-%m-%d')} → {(start + timedelta(days=args.days-1)).strftime('%Y-%m-%d')}")

    total_bytes = 0
    files_written = []
    for d in range(args.days):
        day_dt = start + timedelta(days=d)
        day_epoch = int(day_dt.replace(hour=0, minute=0, second=0).timestamp())
        fname = day_dt.strftime("%Y%m%d") + ".bin"
        out_path = os.path.join(args.output_dir, fname)
        size, n = gen_day_file(out_path, day_epoch, actual_records, interval_sec)
        total_bytes += size
        files_written.append((fname, size, n))
        if not args.quiet:
            print(f"  {fname}: {size:>6,} B ({n} records, ~{size/n:.1f} B/rec)")
        if total_bytes > args.max_bytes:
            print(f"\n[gen] ⚠️  Atingiu --max-bytes ({args.max_bytes}), parando após {d+1} dias.")
            break

    print(f"\n[gen] Total: {len(files_written)} arquivos, {total_bytes:,} B "
          f"({total_bytes/1024:.1f} KB)")
    print(f"[gen] Records totais: {sum(n for _,_,n in files_written):,}")

    if total_bytes > args.max_bytes:
        print(f"[gen] ⚠️  Acima do cap de {args.max_bytes} B — pode encher o FS!")

    return 0

if __name__ == "__main__":
    sys.exit(main())
