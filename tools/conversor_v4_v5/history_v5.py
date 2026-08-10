#!/usr/bin/env python3
"""HistoryV5 — reference implementation of the SIMUT compressed history format.

This module is the normative oracle for the firmware codec: everything the
firmware writes must decode here bit for bit, and everything this module
writes must decode on the firmware. The format itself is specified in
`docs/HistoryV5_Instrucoes_Implementacao.md`; where code and document
disagree, the document wins.

Python 3, standard library only.

Commands
    --selftest                     format test vectors (CRC, zigzag, prefix
                                   boundaries, NAN, resync, RAW fallback)
    --convert IN.bin OUT.h5        legacy 28-byte record file -> V5
    --convert-v4 IN.sim4 OUT.h5    the format the firmware writes today -> V5
    --dump-csv FILE.h5             decode to CSV, header derived from SCHEMA
    --stats FILE [FILE ...]        compression ratio, bits per channel,
                                   symbol histogram
    --synth OUT.h5                 synthetic day of history for bench tests
"""

from __future__ import annotations

import argparse
import csv
import math
import os
import random
import struct
import sys
from dataclasses import dataclass, field
from typing import Iterable, Iterator, Sequence

# ===========================================================================
#  Format constants (§3, §4)
# ===========================================================================

H5_MAGIC = 0x4835                 # "H5" little-endian
H5_VERSION = 0x02                 # on-disk format version
H5_CHUNK_SCHEMA = 0x01
H5_CHUNK_DATA = 0x02

H5_FLAG_RAW = 0x01
H5_FLAG_PARTIAL = 0x02

H5_MAX_CHANNELS = 16
H5_BLOCK_MAX_RECORDS = 60
H5_NAN = -32768                   # 0x8000, same sentinel as the rest of SIMUT

H5_KIND_TEMP_C = 0x01
H5_KIND_HUM_PCT = 0x02
H5_KIND_PRESS_HPA = 0x03
H5_KIND_CO2_PPM = 0x04
H5_KIND_VOC_IDX = 0x05
H5_KIND_GENERIC = 0x7E

KIND_UNIT = {
    H5_KIND_TEMP_C: '°C',
    H5_KIND_HUM_PCT: '%',
    H5_KIND_PRESS_HPA: 'hPa',
    H5_KIND_CO2_PPM: 'ppm',
    H5_KIND_VOC_IDX: 'idx',
    H5_KIND_GENERIC: '',
}

KIND_NAME = {
    H5_KIND_TEMP_C: 'temp',
    H5_KIND_HUM_PCT: 'hum',
    H5_KIND_PRESS_HPA: 'press',
    H5_KIND_CO2_PPM: 'co2',
    H5_KIND_VOC_IDX: 'voc',
    H5_KIND_GENERIC: 'generic',
}


def schema_chunk_size(n: int) -> int:
    return 8 + 4 * n + 2


def data_header_size(n: int) -> int:
    return 16 + 6 * n


def raw_record_size(n: int) -> int:
    return 2 + 2 * n


H5_BLOCK_MAX_BYTES = (data_header_size(H5_MAX_CHANNELS)
                      + (H5_BLOCK_MAX_RECORDS - 1) * raw_record_size(H5_MAX_CHANNELS))


# ===========================================================================
#  CRC-16/CCITT-FALSE (§3.4)
# ===========================================================================

def crc16(data: bytes, crc: int = 0xFFFF) -> int:
    """Poly 0x1021, init 0xFFFF, no reflection, xorout 0x0000.

    Test vector required by the spec: crc16(b'123456789') == 0x29B1.
    """
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


# ===========================================================================
#  Bit I/O — MSB first (§3.5)
# ===========================================================================

class BitWriter:
    """Most significant bit of each byte first; multi-bit fields MSB to LSB."""

    def __init__(self) -> None:
        self._buf = bytearray()
        self._acc = 0
        self._n = 0

    def put(self, value: int, width: int) -> None:
        for i in range(width - 1, -1, -1):
            self._acc = (self._acc << 1) | ((value >> i) & 1)
            self._n += 1
            if self._n == 8:
                self._buf.append(self._acc)
                self._acc = 0
                self._n = 0

    def put_prefix(self, bits: str) -> None:
        for ch in bits:
            self.put(1 if ch == '1' else 0, 1)

    @property
    def bit_count(self) -> int:
        return len(self._buf) * 8 + self._n

    def flush(self) -> bytes:
        """Pad the final byte with zeros. Idempotent only if not written after."""
        if self._n:
            self._buf.append((self._acc << (8 - self._n)) & 0xFF)
            self._acc = 0
            self._n = 0
        return bytes(self._buf)


class BitReader:
    def __init__(self, data: bytes) -> None:
        self._data = data
        self._pos = 0          # bit position
        self.underflow = False  # sticky, like the firmware's flag

    def get(self, width: int) -> int:
        value = 0
        for _ in range(width):
            byte_i = self._pos >> 3
            if byte_i >= len(self._data):
                self.underflow = True
                return value << 1 if width else 0
            bit = (self._data[byte_i] >> (7 - (self._pos & 7))) & 1
            value = (value << 1) | bit
            self._pos += 1
        return value

    def eof(self) -> bool:
        return self._pos >= len(self._data) * 8


def zigzag(d: int) -> int:
    """int16 delta -> unsigned. Mirrors (uint16_t)((d << 1) ^ (d >> 15))."""
    return ((d << 1) ^ (d >> 15)) & 0xFFFF


def unzigzag(z: int) -> int:
    """Unsigned -> int16 delta. Mirrors (int16_t)((z >> 1) ^ -(z & 1))."""
    v = (z >> 1) ^ (-(z & 1) & 0xFFFF)
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def to_i16(v: int) -> int:
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


# ===========================================================================
#  Schema (§3.2)
# ===========================================================================

@dataclass
class ChannelDesc:
    id: int
    kind: int
    scale_exp: int
    flags: int = 0

    def pack(self) -> bytes:
        return struct.pack('<BBbB', self.id, self.kind, self.scale_exp, self.flags)

    @staticmethod
    def unpack(buf: bytes) -> 'ChannelDesc':
        return ChannelDesc(*struct.unpack('<BBbB', buf))

    @property
    def unit(self) -> str:
        return KIND_UNIT.get(self.kind, '')

    def scaled(self, raw: int) -> float:
        return raw * (10.0 ** self.scale_exp)

    def label(self) -> str:
        return f'{KIND_NAME.get(self.kind, f"k{self.kind:02x}")}{self.id}'


def pack_preamble(ctype: int, flags: int, a: int, b: int) -> bytes:
    return struct.pack('<HBBBBBB', H5_MAGIC, H5_VERSION, ctype, flags, a, b, 0xFF)


def build_schema_chunk(schema: Sequence[ChannelDesc], seq: int) -> bytes:
    n = len(schema)
    if not 1 <= n <= H5_MAX_CHANNELS:
        raise ValueError(f'nCh out of range: {n}')
    body = pack_preamble(H5_CHUNK_SCHEMA, 0, n, seq)
    for desc in schema:
        body += desc.pack()
    return body + struct.pack('<H', crc16(body))


# ===========================================================================
#  Block encoder (§3.5, §3.6)
# ===========================================================================

@dataclass
class SymbolStats:
    """Histogram of emitted symbols, for --stats."""
    time: dict = field(default_factory=lambda: {'zero': 0, 's7': 0, 's12': 0, 'resync': 0})
    value: dict = field(default_factory=lambda: {'zero': 0, 'z3': 0, 'z6': 0, 'z10': 0, 'abs': 0})
    value_bits: int = 0
    time_bits: int = 0

    def merge(self, other: 'SymbolStats') -> None:
        for k in self.time:
            self.time[k] += other.time[k]
        for k in self.value:
            self.value[k] += other.value[k]
        self.value_bits += other.value_bits
        self.time_bits += other.time_bits


class BlockEncoder:
    """One DATA block: up to H5_BLOCK_MAX_RECORDS samples of the same schema.

    Mirrors the firmware's HistoryV5Encoder so the two can be diffed sample by
    sample when they disagree.
    """

    def __init__(self, schema: Sequence[ChannelDesc], nominal_interval_s: int = 60) -> None:
        self.schema = list(schema)
        self.n = len(self.schema)
        self.nominal = nominal_interval_s
        self.t0 = 0
        self.keyframe: list[int] = []
        self.samples: list[tuple[int, list[int]]] = []   # records 2..count

    # -- accumulation ----------------------------------------------------
    def reset(self, epoch: int, values: Sequence[int]) -> None:
        if len(values) != self.n:
            raise ValueError('value count does not match the schema')
        self.t0 = int(epoch)
        self.keyframe = [to_i16(v) for v in values]
        self.samples = []

    def add(self, epoch: int, values: Sequence[int]) -> bool:
        if self.count >= H5_BLOCK_MAX_RECORDS:
            return False
        # RAW addresses records by a u16 offset from t0 (§3.6). Outside that
        # reach the block has no RAW form, and an incompressible block with no
        # RAW form has no size bound — §14-1's "a block always fits
        # H5_BLOCK_MAX_BYTES" rests on RAW always being available. Ordinary
        # jitter and NTP corrections still ride the resync symbol inside the
        # block; only a jump past ~18 h closes it.
        if not 0 <= int(epoch) - self.t0 <= 0xFFFF:
            return False
        if len(values) != self.n:
            raise ValueError('value count does not match the schema')
        self.samples.append((int(epoch), [to_i16(v) for v in values]))
        return True

    @property
    def count(self) -> int:
        return 1 + len(self.samples)

    # -- payload variants ------------------------------------------------
    def _compressed(self, stats: SymbolStats | None = None) -> bytes:
        bw = BitWriter()
        prev = list(self.keyframe)
        prev_delta = self.nominal
        prev_epoch = self.t0

        for epoch, values in self.samples:
            # --- time symbol ---
            bits_before = bw.bit_count
            delta = epoch - prev_epoch
            dod = delta - prev_delta
            if dod == 0:
                bw.put_prefix('0')
                if stats:
                    stats.time['zero'] += 1
                prev_delta = delta
            elif -64 <= dod <= 63:
                bw.put_prefix('10')
                bw.put(dod & 0x7F, 7)
                if stats:
                    stats.time['s7'] += 1
                prev_delta = delta
            elif -2048 <= dod <= 2047:
                bw.put_prefix('110')
                bw.put(dod & 0xFFF, 12)
                if stats:
                    stats.time['s12'] += 1
                prev_delta = delta
            else:
                bw.put_prefix('111')
                bw.put(epoch & 0xFFFFFFFF, 32)
                if stats:
                    stats.time['resync'] += 1
                prev_delta = self.nominal
            prev_epoch = epoch
            if stats:
                stats.time_bits += bw.bit_count - bits_before

            # --- value symbols ---
            bits_before = bw.bit_count
            for c in range(self.n):
                cur = values[c]
                p = prev[c]
                if p == H5_NAN and cur == H5_NAN:
                    # Channel stays NAN: 1 bit, no delta arithmetic.
                    bw.put_prefix('0')
                    if stats:
                        stats.value['zero'] += 1
                elif p == H5_NAN or cur == H5_NAN:
                    # Either edge of the NAN transition restarts the chain.
                    bw.put_prefix('1111')
                    bw.put(cur & 0xFFFF, 16)
                    if stats:
                        stats.value['abs'] += 1
                else:
                    d = cur - p
                    if d == 0:
                        bw.put_prefix('0')
                        if stats:
                            stats.value['zero'] += 1
                    elif -4 <= d <= 3:
                        bw.put_prefix('10')
                        bw.put(zigzag(d), 3)
                        if stats:
                            stats.value['z3'] += 1
                    elif -32 <= d <= 31:
                        bw.put_prefix('110')
                        bw.put(zigzag(d), 6)
                        if stats:
                            stats.value['z6'] += 1
                    elif -512 <= d <= 511:
                        bw.put_prefix('1110')
                        bw.put(zigzag(d), 10)
                        if stats:
                            stats.value['z10'] += 1
                    else:
                        bw.put_prefix('1111')
                        bw.put(cur & 0xFFFF, 16)
                        if stats:
                            stats.value['abs'] += 1
                prev[c] = cur
            if stats:
                stats.value_bits += bw.bit_count - bits_before

        return bw.flush()

    def _raw(self) -> bytes | None:
        """RAW payload, or None when a dt does not fit the u16 field."""
        out = bytearray()
        for epoch, values in self.samples:
            dt = epoch - self.t0
            if not 0 <= dt <= 0xFFFF:
                return None
            out += struct.pack('<H', dt)
            for v in values:
                out += struct.pack('<h', v)
        return bytes(out)

    # -- sealing ---------------------------------------------------------
    def envelope(self) -> tuple[list[int], list[int]]:
        """Per-channel min/max over the whole block; 0x8000 when all NAN."""
        mins = [H5_NAN] * self.n
        maxs = [H5_NAN] * self.n
        for _, values in [(self.t0, self.keyframe)] + self.samples:
            for c, v in enumerate(values):
                if v == H5_NAN:
                    continue
                if mins[c] == H5_NAN or v < mins[c]:
                    mins[c] = v
                if maxs[c] == H5_NAN or v > maxs[c]:
                    maxs[c] = v
        return mins, maxs

    def seal(self, extra_flags: int = 0, stats: SymbolStats | None = None) -> bytes:
        if not self.keyframe:
            raise ValueError('seal() before reset()')
        comp = self._compressed(stats)
        raw = self._raw()
        use_raw = raw is not None and len(raw) < len(comp)
        payload = raw if use_raw else comp
        flags = extra_flags | (H5_FLAG_RAW if use_raw else 0)

        mins, maxs = self.envelope()
        pre = pack_preamble(H5_CHUNK_DATA, flags, self.count, self.n)
        fixed = pre + struct.pack('<IH', self.t0 & 0xFFFFFFFF, len(payload))
        tails = b''.join(struct.pack('<h', v) for v in self.keyframe)
        tails += b''.join(struct.pack('<h', v) for v in mins)
        tails += b''.join(struct.pack('<h', v) for v in maxs)
        # CRC covers everything but its own two bytes (offset 14..15).
        crc = crc16(fixed + tails + payload)
        return fixed + struct.pack('<H', crc) + tails + payload


# ===========================================================================
#  Block decoder
# ===========================================================================

@dataclass
class DataHeader:
    flags: int
    count: int
    n: int
    t0: int
    payload_len: int
    crc: int
    keyframe: list[int]
    ch_min: list[int]
    ch_max: list[int]

    @property
    def is_raw(self) -> bool:
        return bool(self.flags & H5_FLAG_RAW)

    @property
    def is_partial(self) -> bool:
        return bool(self.flags & H5_FLAG_PARTIAL)


def parse_data_header(chunk: bytes) -> DataHeader:
    magic, version, ctype, flags, a, b, rsv = struct.unpack('<HBBBBBB', chunk[:8])
    if magic != H5_MAGIC or version != H5_VERSION or ctype != H5_CHUNK_DATA:
        raise ValueError('not a V5 DATA chunk')
    t0, payload_len, crc = struct.unpack('<IHH', chunk[8:16])
    n = b
    need = data_header_size(n)
    if len(chunk) < need:
        raise ValueError('truncated DATA header')
    tails = chunk[16:need]
    ints = struct.unpack(f'<{3 * n}h', tails)
    return DataHeader(flags, a, n, t0, payload_len, crc,
                      list(ints[0:n]), list(ints[n:2 * n]), list(ints[2 * n:3 * n]))


def verify_data_crc(chunk: bytes, hdr: DataHeader) -> bool:
    total = data_header_size(hdr.n) + hdr.payload_len
    if len(chunk) < total:
        return False
    body = chunk[:14] + chunk[16:total]
    return crc16(body) == hdr.crc


def decode_block(chunk: bytes, schema: Sequence[ChannelDesc],
                 nominal_interval_s: int = 60) -> list[tuple[int, list[int]]]:
    """Decode a validated DATA chunk into [(epoch, values), ...]."""
    hdr = parse_data_header(chunk)
    n = hdr.n
    if n != len(schema):
        raise ValueError(f'DATA nCh={n} does not match the SCHEMA nCh={len(schema)}')
    out = [(hdr.t0, list(hdr.keyframe))]
    if hdr.count == 1:
        return out

    start = data_header_size(n)
    payload = chunk[start:start + hdr.payload_len]

    if hdr.is_raw:
        rec = raw_record_size(n)
        for i in range(hdr.count - 1):
            blob = payload[i * rec:(i + 1) * rec]
            dt = struct.unpack('<H', blob[:2])[0]
            vals = list(struct.unpack(f'<{n}h', blob[2:]))
            out.append((hdr.t0 + dt, vals))
        return out

    br = BitReader(payload)
    prev = list(hdr.keyframe)
    prev_delta = nominal_interval_s
    prev_epoch = hdr.t0
    for _ in range(hdr.count - 1):
        # --- time symbol ---
        if br.get(1) == 0:
            delta = prev_delta
            epoch = prev_epoch + delta
            prev_delta = delta
        elif br.get(1) == 0:
            dod = br.get(7)
            dod -= 128 if dod & 0x40 else 0
            delta = prev_delta + dod
            epoch = prev_epoch + delta
            prev_delta = delta
        elif br.get(1) == 0:
            dod = br.get(12)
            dod -= 4096 if dod & 0x800 else 0
            delta = prev_delta + dod
            epoch = prev_epoch + delta
            prev_delta = delta
        else:
            epoch = br.get(32)
            prev_delta = nominal_interval_s
        prev_epoch = epoch

        # --- value symbols ---
        vals = []
        for c in range(n):
            if br.get(1) == 0:
                v = prev[c]
            elif br.get(1) == 0:
                v = prev[c] + unzigzag(br.get(3))
            elif br.get(1) == 0:
                v = prev[c] + unzigzag(br.get(6))
            elif br.get(1) == 0:
                v = prev[c] + unzigzag(br.get(10))
            else:
                v = to_i16(br.get(16))
            v = to_i16(v)
            prev[c] = v
            vals.append(v)
        out.append((epoch, vals))
    return out


# ===========================================================================
#  File level: writer, scanner, reader
# ===========================================================================

class FileWriter:
    """Builds a whole `.h5` day: SCHEMA chunk, then DATA chunks."""

    def __init__(self, schema: Sequence[ChannelDesc], nominal_interval_s: int = 60) -> None:
        self.schema = list(schema)
        self.nominal = nominal_interval_s
        self.seq = 0
        self.blob = bytearray(build_schema_chunk(self.schema, 0))
        self.enc = BlockEncoder(self.schema, nominal_interval_s)
        self.open_block = False
        self.stats = SymbolStats()
        self.blocks = 0

    def new_schema(self, schema: Sequence[ChannelDesc]) -> None:
        """§3.7-2: seal the current block PARTIAL, then start a new SCHEMA."""
        if list(schema) == self.schema:
            return                              # §14-7: never write a twin
        self._seal(H5_FLAG_PARTIAL)
        self.seq += 1
        self.schema = list(schema)
        self.blob += build_schema_chunk(self.schema, self.seq)
        self.enc = BlockEncoder(self.schema, self.nominal)

    def append(self, epoch: int, values: Sequence[int]) -> None:
        if not self.open_block:
            self.enc.reset(epoch, values)
            self.open_block = True
            return
        if not self.enc.add(epoch, values):
            # Full, or the record fell outside RAW's reach from t0. Either way
            # the block closes; only a full one is not PARTIAL.
            self._seal(0 if self.enc.count >= H5_BLOCK_MAX_RECORDS else H5_FLAG_PARTIAL)
            self.enc.reset(epoch, values)
            self.open_block = True

    def _seal(self, flags: int) -> None:
        if not self.open_block:
            return
        self.blob += self.enc.seal(flags, self.stats)
        self.blocks += 1
        self.open_block = False

    def close(self) -> bytes:
        # A block that never filled its 60 slots is PARTIAL by definition.
        if self.open_block and self.enc.count < H5_BLOCK_MAX_RECORDS:
            self._seal(H5_FLAG_PARTIAL)
        else:
            self._seal(0)
        return bytes(self.blob)


@dataclass
class ScanEntry:
    kind: str                 # 'schema' | 'data'
    offset: int
    size: int
    schema: list[ChannelDesc] | None = None
    seq: int = 0
    header: DataHeader | None = None


def scan(blob: bytes, on_error=None) -> Iterator[ScanEntry]:
    """Walk chunks without decoding payloads. Skips over invalid chunks.

    Yields SCHEMA and DATA entries in file order. A chunk that fails magic,
    version or CRC is reported through `on_error` and skipped — never
    propagated, never partially read (§3.7-4).
    """
    pos = 0
    while pos + 8 <= len(blob):
        magic, version, ctype, flags, a, b, rsv = struct.unpack('<HBBBBBB', blob[pos:pos + 8])
        if magic != H5_MAGIC or version != H5_VERSION:
            if on_error:
                on_error(pos, 'bad magic/version')
            return                      # resyncing on garbage is worse than stopping
        if ctype == H5_CHUNK_SCHEMA:
            n = a
            size = schema_chunk_size(n)
            if pos + size > len(blob):
                if on_error:
                    on_error(pos, 'truncated SCHEMA')
                return
            body = blob[pos:pos + 8 + 4 * n]
            stored = struct.unpack('<H', blob[pos + 8 + 4 * n:pos + size])[0]
            if crc16(body) != stored:
                if on_error:
                    on_error(pos, 'SCHEMA CRC')
            else:
                descs = [ChannelDesc.unpack(body[8 + 4 * i:12 + 4 * i]) for i in range(n)]
                yield ScanEntry('schema', pos, size, schema=descs, seq=b)
            pos += size
        elif ctype == H5_CHUNK_DATA:
            n = b
            hsize = data_header_size(n)
            if pos + hsize > len(blob):
                if on_error:
                    on_error(pos, 'truncated DATA header')
                return
            chunk_head = blob[pos:pos + hsize]
            hdr = parse_data_header(chunk_head)
            size = hsize + hdr.payload_len
            if pos + size > len(blob):
                if on_error:
                    on_error(pos, 'truncated DATA payload')
                return
            if not verify_data_crc(blob[pos:pos + size], hdr):
                if on_error:
                    on_error(pos, 'DATA CRC')
            else:
                yield ScanEntry('data', pos, size, header=hdr)
            pos += size
        else:
            if on_error:
                on_error(pos, f'unknown chunk type 0x{ctype:02x}')
            return


def read_series(blob: bytes, nominal_interval_s: int = 60,
                on_error=None) -> Iterator[tuple[list[ChannelDesc], int, list[int]]]:
    """Yield (schema, epoch, values) for every record in the file."""
    schema: list[ChannelDesc] | None = None
    for entry in scan(blob, on_error):
        if entry.kind == 'schema':
            schema = entry.schema
            continue
        if schema is None:
            if on_error:
                on_error(entry.offset, 'DATA before any SCHEMA')
            continue
        if entry.header.n != len(schema):
            if on_error:
                on_error(entry.offset, 'DATA nCh does not match the SCHEMA')
            continue
        chunk = blob[entry.offset:entry.offset + entry.size]
        for epoch, values in decode_block(chunk, schema, nominal_interval_s):
            yield schema, epoch, values


# ===========================================================================
#  Legacy readers (converter only — the firmware never reads these)
# ===========================================================================

LEGACY_RECORD_SIZE = 28           # epoch u32 + 12 x int16

# The §4 table, used only by --convert: the legacy file has no schema of its
# own, so the converter has to supply one.
LEGACY_SCHEMA = [ChannelDesc(i, H5_KIND_TEMP_C, -2) for i in range(11)] + \
                [ChannelDesc(11, H5_KIND_HUM_PCT, -2)]


def read_legacy_bin(path: str) -> Iterator[tuple[int, list[int]]]:
    with open(path, 'rb') as fh:
        while True:
            blob = fh.read(LEGACY_RECORD_SIZE)
            if len(blob) < LEGACY_RECORD_SIZE:
                return
            epoch = struct.unpack('<I', blob[:4])[0]
            yield epoch, list(struct.unpack('<12h', blob[4:]))


# --- V4 (.sim4) — the format the firmware writes before this change --------

V4_MAGIC = b'SIM4'
V4_CH_TEMP, V4_CH_HUM, V4_CH_PRESS, V4_CH_LUX = 0, 1, 2, 3

# (bit width, scale) per channel, from src/sensors/SensorChannelTable.h.
V4_CHANNEL = {
    V4_CH_TEMP: (16, 100, True),
    V4_CH_HUM: (10, 10, False),
    V4_CH_PRESS: (14, 10, False),
    V4_CH_LUX: (24, 100, False),
}

V4_KIND = {
    V4_CH_TEMP: H5_KIND_TEMP_C,
    V4_CH_HUM: H5_KIND_HUM_PCT,
    V4_CH_PRESS: H5_KIND_PRESS_HPA,
    V4_CH_LUX: H5_KIND_GENERIC,
}


@dataclass
class V4Measure:
    sensor_idx: int
    channel: int
    bit_width: int
    decimals: int
    unit: str
    scale: int
    hw_id: str
    bit_offset: int = 0


@dataclass
class V4Schema:
    measures: list[V4Measure]
    anchor_period: int
    header_len: int
    anchor_bytes: int


# The two tables are memcpy'd straight out of RAM, so they carry the compiler's
# native layout: HistV4SensorDef is 9 packed uint8_t, HistV4MeasureDef is six
# uint8_t plus two bytes of padding before its 4-byte-aligned uint32 scale.
V4_SENSOR_FMT = '<9B'
V4_SENSOR_SIZE = struct.calcsize(V4_SENSOR_FMT)
V4_MEASURE_FMT = '<6B2xI'
V4_MEASURE_SIZE = struct.calcsize(V4_MEASURE_FMT)


def read_v4_schema(blob: bytes) -> V4Schema:
    if blob[:4] != V4_MAGIC:
        raise ValueError('not a .sim4 file')
    (version, header_size, anchor_period, sensor_count, measure_count,
     flags, strpool_size, _rsv) = struct.unpack('<HHHBBBBH', blob[4:16])
    pos = 16
    sensors = []
    for _ in range(sensor_count):
        fields = struct.unpack(V4_SENSOR_FMT, blob[pos:pos + V4_SENSOR_SIZE])
        sensors.append((fields[0], fields[1]))
        pos += V4_SENSOR_SIZE
    measures_raw = []
    for _ in range(measure_count):
        (sidx, ch, bw, dec, uoff, ulen, scale) = struct.unpack(
            V4_MEASURE_FMT, blob[pos:pos + V4_MEASURE_SIZE])
        measures_raw.append((sidx, ch, bw, dec, uoff, ulen, scale))
        pos += V4_MEASURE_SIZE
    if pos + strpool_size != header_size:
        raise ValueError(f'header size {header_size} does not match the parsed '
                         f'tables (ends at {pos + strpool_size})')
    pool = blob[pos:pos + strpool_size]

    def s(off, ln):
        return pool[off:off + ln].decode('utf-8', 'replace')

    measures = []
    bit_off = 32                     # epoch occupies bits 0..31 of an anchor
    for (sidx, ch, bw, dec, uoff, ulen, scale) in measures_raw:
        hw_off, hw_len = sensors[sidx]
        measures.append(V4Measure(sidx, ch, bw, dec, s(uoff, ulen), scale,
                                  s(hw_off, hw_len), bit_off))
        bit_off += bw
    anchor_bytes = (bit_off + 7) // 8
    return V4Schema(measures, anchor_period, header_size, anchor_bytes)


def _v4_nan(bw: int) -> int:
    """V4's sentinel is all-ones in the field's own width."""
    return (1 << bw) - 1


def _v4_bit_extract(blob: bytes, bit_off: int, width: int) -> int:
    """V4 packs LSB-first within each byte — the opposite of the V5 bitstream."""
    byte_i = bit_off >> 3
    shift = bit_off & 7
    needed = (shift + width + 7) // 8
    raw = int.from_bytes(blob[byte_i:byte_i + needed], 'little')
    return (raw >> shift) & ((1 << width) - 1)


def _v4_read_varint_z(blob: bytes, pos: int) -> tuple[int, int]:
    shift = 0
    result = 0
    start = pos
    while True:
        if pos >= len(blob):
            raise EOFError
        byte = blob[pos]
        pos += 1
        result |= (byte & 0x7F) << shift
        if not byte & 0x80:
            break
        shift += 7
        if shift > 35:
            raise ValueError('varint too long')
    value = (result >> 1) ^ -(result & 1)
    return value, pos - start


def read_v4_records(blob: bytes) -> tuple[V4Schema, list[tuple[int, list[int | None]]]]:
    """Decode a .sim4 into [(epoch, [raw or None, ...])].

    Follows histV4Decode: bit-packed anchors every `anchorPeriod` records,
    delta records carrying a presence mask plus zigzag varints, and the
    unsigned-channel fix-up that keeps 76.5 % humidity from decoding negative.
    """
    schema = read_v4_schema(blob)
    n = len(schema.measures)
    pos = schema.header_len
    last = [0] * n
    has_valid = [False] * n
    since_anchor = 0
    last_epoch = 0
    out: list[tuple[int, list[int | None]]] = []
    mask_bytes = (n + 7) // 8

    while pos < len(blob):
        is_anchor = since_anchor == 0 or since_anchor >= schema.anchor_period
        if is_anchor:
            if pos + schema.anchor_bytes > len(blob):
                break
            rec = blob[pos:pos + schema.anchor_bytes]
            epoch = _v4_bit_extract(rec, 0, 32)
            vals: list[int | None] = []
            for i, m in enumerate(schema.measures):
                raw = _v4_bit_extract(rec, m.bit_offset, m.bit_width)
                nan = _v4_nan(m.bit_width)
                if raw == nan:
                    vals.append(None)
                    last[i] = raw
                    has_valid[i] = False
                    continue
                signed = V4_CHANNEL.get(m.channel, (16, 100, True))[2]
                if signed and raw & (1 << (m.bit_width - 1)):
                    raw -= 1 << m.bit_width
                vals.append(raw)
                last[i] = raw
                has_valid[i] = True
            pos += schema.anchor_bytes
            since_anchor = 1
            last_epoch = epoch
            out.append((epoch, vals))
            continue

        if pos + mask_bytes + 1 > len(blob):
            break
        mask = blob[pos:pos + mask_bytes]
        p = pos + mask_bytes
        try:
            d_epoch, used = _v4_read_varint_z(blob, p)
        except (EOFError, ValueError):
            break
        p += used
        epoch = (last_epoch + d_epoch) & 0xFFFFFFFF
        vals = [None if not has_valid[i] else last[i] for i in range(n)]
        ok = True
        staged = {}
        for i, m in enumerate(schema.measures):
            if not mask[i >> 3] & (1 << (i & 7)):
                continue
            try:
                decoded, used = _v4_read_varint_z(blob, p)
            except (EOFError, ValueError):
                ok = False
                break
            p += used
            v = last[i] + decoded if has_valid[i] else decoded
            staged[i] = v
        if not ok:
            break
        for i, v in staged.items():
            m = schema.measures[i]
            nan = _v4_nan(m.bit_width)
            if (v & ((1 << m.bit_width) - 1)) == nan:
                vals[i] = None
                last[i] = v
                has_valid[i] = False
            else:
                vals[i] = v
                last[i] = v
                has_valid[i] = True
        out.append((epoch, vals))
        last_epoch = epoch
        since_anchor += 1
        pos = p
    return schema, out


def v4_schema_to_v5(schema: V4Schema) -> list[ChannelDesc]:
    """Map a .sim4 measurement table onto V5 channel descriptors.

    `id` is the measurement's index in the file's own table; the SCHEMA chunk
    is the authority for what each channel means, so a converted file is
    self-describing without needing the .sim4 alongside it.
    """
    out = []
    for i, m in enumerate(schema.measures):
        scale_exp = -int(round(math.log10(m.scale))) if m.scale > 0 else 0
        out.append(ChannelDesc(i, V4_KIND.get(m.channel, H5_KIND_GENERIC), scale_exp))
    return out


# ===========================================================================
#  Commands
# ===========================================================================

def _write_series(out_path: str, schema: Sequence[ChannelDesc],
                  series: Iterable[tuple[int, Sequence[int]]],
                  interval: int = 60) -> tuple[int, int]:
    fw = FileWriter(schema, interval)
    count = 0
    for epoch, values in series:
        fw.append(epoch, values)
        count += 1
    blob = fw.close()
    with open(out_path, 'wb') as fh:
        fh.write(blob)
    return count, len(blob)


def cmd_convert(args) -> int:
    series = list(read_legacy_bin(args.convert[0]))
    if not series:
        print('empty or unreadable legacy file', file=sys.stderr)
        return 1
    src = os.path.getsize(args.convert[0])
    count, size = _write_series(args.convert[1], LEGACY_SCHEMA, series, args.interval)
    print(f'{count} records  {src} B -> {size} B  ratio {src / size:.2f}x')
    return 0


def cmd_convert_v4(args) -> int:
    blob = open(args.convert_v4[0], 'rb').read()
    v4, records = read_v4_records(blob)
    if not records:
        print('no records decoded from the .sim4', file=sys.stderr)
        return 1
    schema = v4_schema_to_v5(v4)
    series = [(epoch, [H5_NAN if v is None else max(-32767, min(32767, v)) for v in vals])
              for epoch, vals in records]
    count, size = _write_series(args.convert_v4[1], schema, series, args.interval)
    src = len(blob)
    print(f'{count} records, {len(schema)} channels  '
          f'{src} B (.sim4) -> {size} B (.h5)  ratio {src / size:.2f}x')
    return 0


def cmd_dump_csv(args) -> int:
    blob = open(args.dump_csv, 'rb').read()
    errors = []
    writer = csv.writer(sys.stdout if not args.out else open(args.out, 'w', newline=''))
    header_written = False
    rows = 0
    for schema, epoch, values in read_series(blob, args.interval,
                                             lambda o, m: errors.append((o, m))):
        if not header_written:
            writer.writerow(['epoch'] + [f'{d.label()} [{d.unit}]' if d.unit
                                         else d.label() for d in schema])
            header_written = True
        cells = []
        for d, v in zip(schema, values):
            cells.append('' if v == H5_NAN else f'{d.scaled(v):.{max(0, -d.scale_exp)}f}')
        writer.writerow([epoch] + cells)
        rows += 1
    for off, msg in errors:
        print(f'# skipped chunk at {off}: {msg}', file=sys.stderr)
    print(f'# {rows} records', file=sys.stderr)
    return 0


def cmd_stats(args) -> int:
    for path in args.stats:
        blob = open(path, 'rb').read()
        errors = []
        n_schema = n_data = n_raw = n_partial = 0
        records = 0
        payload_bytes = 0
        header_bytes = 0
        nch = 0
        for entry in scan(blob, lambda o, m: errors.append((o, m))):
            if entry.kind == 'schema':
                n_schema += 1
                nch = len(entry.schema)
                header_bytes += entry.size
                continue
            n_data += 1
            hdr = entry.header
            records += hdr.count
            payload_bytes += hdr.payload_len
            header_bytes += data_header_size(hdr.n)
            n_raw += 1 if hdr.is_raw else 0
            n_partial += 1 if hdr.is_partial else 0

        # Re-encode to recover the symbol histogram the file does not store.
        stats = SymbolStats()
        series = [(e, v) for _, e, v in read_series(blob, args.interval)]
        if series:
            fw = FileWriter([ChannelDesc(i, H5_KIND_GENERIC, 0) for i in range(nch)],
                            args.interval)
            for e, v in series:
                fw.append(e, v)
            fw.close()
            stats = fw.stats

        total = len(blob)
        legacy = records * (4 + 2 * nch) if nch else 0
        print(f'--- {path}')
        print(f'  {total} B, {records} records, {nch} channels, '
              f'{n_schema} SCHEMA, {n_data} DATA ({n_raw} RAW, {n_partial} PARTIAL)')
        if errors:
            print(f'  {len(errors)} chunk(s) skipped: {errors[:3]}')
        if records:
            print(f'  {total / records:.2f} B/record   '
                  f'headers {header_bytes} B ({100 * header_bytes / total:.1f}%)   '
                  f'payload {payload_bytes} B')
            print(f'  vs flat epoch+int16 ({legacy} B): {legacy / total:.2f}x')
            per_day = total / records * 1440
            print(f'  projected {per_day / 1024:.2f} KiB/day at 1 rec/min  '
                  f'-> {1048576 * 0.86 / per_day:.0f} days in 1 MiB at 86%')
        if stats.value_bits:
            vals = sum(stats.value.values())
            print(f'  value symbols: ' + '  '.join(
                f'{k}={v} ({100 * v / vals:.1f}%)' for k, v in stats.value.items()))
            print(f'  time symbols:  ' + '  '.join(
                f'{k}={v}' for k, v in stats.time.items()))
            print(f'  {stats.value_bits / vals:.2f} bits/value, '
                  f'{stats.time_bits / max(1, sum(stats.time.values())):.2f} bits/timestamp')
    return 0


DEFAULT_SYNTH_SCHEMA = [
    ChannelDesc(0, H5_KIND_TEMP_C, -2),      # DS18B20
    ChannelDesc(4, H5_KIND_TEMP_C, -2),      # DS18B20
    ChannelDesc(12, H5_KIND_TEMP_C, -2),     # DHT22 temp
    ChannelDesc(13, H5_KIND_HUM_PCT, -1),    # DHT22 hum
    ChannelDesc(16, H5_KIND_TEMP_C, -2),     # BMP280 temp
    ChannelDesc(18, H5_KIND_PRESS_HPA, -1),  # BMP280 press
    ChannelDesc(40, H5_KIND_TEMP_C, -2),     # DHT22 temp
    ChannelDesc(41, H5_KIND_HUM_PCT, -1),    # DHT22 hum
]


def schema_from_file(path: str) -> list[ChannelDesc]:
    """First SCHEMA chunk of a `.h5` — the device's own channel set."""
    blob = open(path, 'rb').read()
    for entry in scan(blob):
        if entry.kind == 'schema':
            return entry.schema
    raise SystemExit(f'{path}: no SCHEMA chunk')


def synth_series(schema, epoch, records, interval, rnd, dropouts):
    """Plausible bench data: diurnal swing, slow drift, sensor noise, dropouts."""
    series = []
    for i in range(records):
        t = epoch + i * interval
        phase = 2 * math.pi * (i / (86400 / interval))
        base = 22.0 + 4.0 * math.sin(phase)
        vals = []
        for d in schema:
            if d.kind == H5_KIND_TEMP_C:
                v = base + rnd.gauss(0, 0.04) + (d.id % 5) * 0.3
            elif d.kind == H5_KIND_HUM_PCT:
                v = 70.0 - 3.0 * math.sin(phase) + rnd.gauss(0, 0.2)
            elif d.kind == H5_KIND_PRESS_HPA:
                v = 1013.0 + 2.0 * math.sin(phase / 3) + rnd.gauss(0, 0.1)
            else:
                v = 100.0 + 10.0 * math.sin(phase) + rnd.gauss(0, 1.0)
            raw = int(round(v * (10.0 ** -d.scale_exp)))
            raw = max(-32767, min(32767, raw))
            # Intermittent dropouts, so NAN runs are exercised end to end.
            if dropouts and rnd.random() < 0.004:
                raw = H5_NAN
            vals.append(raw)
        series.append((t, vals))
    return series


def cmd_synth(args) -> int:
    rnd = random.Random(args.seed)
    schema = (schema_from_file(args.schema_from) if args.schema_from
              else DEFAULT_SYNTH_SCHEMA)

    if args.days <= 1:
        series = synth_series(schema, args.epoch, args.records, args.interval,
                              rnd, args.dropouts)
        count, size = _write_series(args.synth, schema, series, args.interval)
        print(f'{count} records, {len(schema)} channels -> {size} B '
              f'({size / count:.2f} B/record)')
        return 0

    # Multi-day: `--synth` names a directory and each day gets YYYYMMDD.h5,
    # which is the layout the device expects under /history.
    import datetime
    os.makedirs(args.synth, exist_ok=True)
    per_day = 86400 // args.interval
    total = 0
    for d in range(args.days):
        # Local midnight, because the device names files by local day.
        day_epoch = args.epoch + d * 86400
        name = datetime.datetime.fromtimestamp(day_epoch).strftime('%Y%m%d')
        series = synth_series(schema, day_epoch, per_day, args.interval,
                              rnd, args.dropouts)
        path = os.path.join(args.synth, f'{name}.h5')
        count, size = _write_series(path, schema, series, args.interval)
        total += size
        print(f'{name}.h5  {count} records  {size} B  ({size / count:.2f} B/record)')
    print(f'{args.days} days, {len(schema)} channels, {total} B total '
          f'({total / args.days / 1024:.2f} KiB/day)')
    return 0


# ===========================================================================
#  Selftest (§12)
# ===========================================================================

def _check(name: str, cond: bool, detail: str = '') -> bool:
    print(f'  [{"ok" if cond else "FAIL"}] {name}{"  " + detail if detail else ""}')
    return cond


def cmd_selftest(args) -> int:
    ok = True
    print('CRC-16/CCITT-FALSE')
    ok &= _check('crc16("123456789") == 0x29B1', crc16(b'123456789') == 0x29B1,
                 f'got 0x{crc16(b"123456789"):04X}')
    ok &= _check('crc16(b"") == 0xFFFF', crc16(b'') == 0xFFFF)

    print('zigzag')
    ok &= _check('roundtrip over the whole int16 range',
                 all(unzigzag(zigzag(d)) == d for d in range(-32768, 32768)))
    ok &= _check('zigzag(0,-1,1,-2)= 0,1,2,3',
                 [zigzag(x) for x in (0, -1, 1, -2)] == [0, 1, 2, 3])

    print('bit I/O')
    bw = BitWriter()
    bw.put(0b1, 1); bw.put(0b011, 3); bw.put(0xABCD, 16); bw.put(0b11, 2)
    blob = bw.flush()
    br = BitReader(blob)
    ok &= _check('MSB-first roundtrip across byte boundaries',
                 (br.get(1), br.get(3), br.get(16), br.get(2)) == (1, 0b011, 0xABCD, 0b11))
    ok &= _check('first byte is MSB-packed', blob[0] == 0b10111010,
                 f'got 0b{blob[0]:08b}')
    br2 = BitReader(b'\x00')
    br2.get(8); br2.get(4)
    ok &= _check('underflow is sticky', br2.underflow)

    print('value prefix boundaries')
    schema = [ChannelDesc(0, H5_KIND_GENERIC, 0)]
    cases = {
        'delta 0 -> 1 bit': (0, 1),
        'delta +3 -> 5 bits': (3, 5),
        'delta -4 -> 5 bits': (-4, 5),
        'delta +4 -> 9 bits': (4, 9),
        'delta +31 -> 9 bits': (31, 9),
        'delta -32 -> 9 bits': (-32, 9),
        'delta +32 -> 14 bits': (32, 14),
        'delta +511 -> 14 bits': (511, 14),
        'delta -512 -> 14 bits': (-512, 14),
        'delta +512 -> 20 bits': (512, 20),
    }
    for name, (delta, want_bits) in cases.items():
        enc = BlockEncoder(schema, 60)
        enc.reset(1000, [0])
        enc.add(1060, [delta])
        payload = enc._compressed()
        # 1 bit of time symbol (dod = 0) + the value symbol
        got = 1 + want_bits
        ok &= _check(name, len(payload) == (got + 7) // 8,
                     f'{len(payload)} B for {got} bits')

    print('roundtrip of every representable delta')
    enc = BlockEncoder(schema, 60)
    enc.reset(1000, [0])
    seq = [0]
    v = 0
    for d in (0, 1, -1, 3, -4, 4, -32, 31, 32, -512, 511, 512, -513, 20000, -20000):
        nv = max(-32767, min(32767, v + d))
        seq.append(nv)
        enc.add(1000 + 60 * len(seq) - 60, [nv])
        v = nv
    chunk = enc.seal()
    got = [vals[0] for _, vals in decode_block(chunk, schema)]
    ok &= _check('decode(encode(deltas)) is bit-exact', got == seq,
                 f'{got} vs {seq}')

    print('NAN semantics')
    enc = BlockEncoder(schema, 60)
    enc.reset(1000, [2350])
    nan_seq = [2350, 2351, H5_NAN, H5_NAN, H5_NAN, 2360, 2361, H5_NAN, 2400]
    for i, v in enumerate(nan_seq[1:], start=1):
        enc.add(1000 + 60 * i, [v])
    chunk = enc.seal()
    got = [vals[0] for _, vals in decode_block(chunk, schema)]
    ok &= _check('enter/stay/leave NAN survives roundtrip', got == nan_seq,
                 f'{got}')
    hdr = parse_data_header(chunk)
    ok &= _check('envelope ignores NAN', (hdr.ch_min[0], hdr.ch_max[0]) == (2350, 2400),
                 f'min={hdr.ch_min[0]} max={hdr.ch_max[0]}')

    enc = BlockEncoder(schema, 60)
    enc.reset(1000, [H5_NAN])
    for i in range(1, 10):
        enc.add(1000 + 60 * i, [H5_NAN])
    chunk = enc.seal()
    hdr = parse_data_header(chunk)
    ok &= _check('all-NAN channel reports min=max=0x8000',
                 (hdr.ch_min[0], hdr.ch_max[0]) == (H5_NAN, H5_NAN))
    ok &= _check('all-NAN block costs 1 bit per record',
                 hdr.payload_len == (9 * 2 + 7) // 8, f'{hdr.payload_len} B')

    print('time symbols')
    t0 = 1_700_000_000
    enc = BlockEncoder(schema, 60)
    enc.reset(t0, [0])
    #        dod 0     dod 0     dod +3    dod -3    dod +97   resync    dod 0
    times = [t0 + 60, t0 + 120, t0 + 183, t0 + 243, t0 + 400, t0 + 40000, t0 + 40060]
    for t in times:
        ok &= _check(f'add({t - t0} s after t0) accepted', enc.add(t, [0]))
    chunk = enc.seal()
    got = [e for e, _ in decode_block(chunk, schema)]
    ok &= _check('dod 0 / ±7bit / ±12bit / resync roundtrip',
                 got == [t0] + times, f'{got}')

    enc = BlockEncoder(schema, 60)
    enc.reset(t0, [0])
    enc.add(t0 + 60, [0])       # dod 0
    enc.add(t0 + 40000, [0])    # forces resync
    enc.add(t0 + 40060, [0])    # prevDelta must be back to nominal -> 1 bit
    payload = enc._compressed()
    # (dod=0) + (resync) + (dod=0), each followed by one unchanged value.
    want_bits = (1 + 1) + (35 + 1) + (1 + 1)
    ok &= _check('resync restores prevDelta to nominal',
                 len(payload) == (want_bits + 7) // 8,
                 f'{len(payload)} B for {want_bits} bits')

    ok &= _check('add() refuses a record RAW could not address',
                 not enc.add(t0 + 70000, [0]) and not enc.add(t0 - 1, [0]))

    print('RAW fallback (§3.6)')
    rnd = random.Random(7)
    wide = [ChannelDesc(i, H5_KIND_GENERIC, 0) for i in range(12)]
    enc = BlockEncoder(wide, 60)
    enc.reset(1000, [rnd.randint(-30000, 30000) for _ in range(12)])
    expect = [list(enc.keyframe)]
    for i in range(1, 60):
        vals = [rnd.randint(-30000, 30000) for _ in range(12)]
        enc.add(1000 + 60 * i, vals)
        expect.append(vals)
    chunk = enc.seal()
    hdr = parse_data_header(chunk)
    ok &= _check('incompressible block picks RAW', hdr.is_raw)
    ok &= _check('RAW payload is (count-1) * (2 + 2n)',
                 hdr.payload_len == 59 * raw_record_size(12), f'{hdr.payload_len}')
    got = [v for _, v in decode_block(chunk, wide)]
    ok &= _check('RAW roundtrip is bit-exact', got == expect)
    ok &= _check('worst case stays under H5_BLOCK_MAX_BYTES',
                 len(chunk) <= H5_BLOCK_MAX_BYTES, f'{len(chunk)} B')

    print('chunk framing and accessors')
    for n in (1, 3, 12, 16):
        s = [ChannelDesc(i, H5_KIND_TEMP_C, -2) for i in range(n)]
        enc = BlockEncoder(s, 60)
        enc.reset(1000, [100 + i for i in range(n)])
        enc.add(1060, [101 + i for i in range(n)])
        chunk = enc.seal()
        hdr = parse_data_header(chunk)
        ok &= _check(f'nCh={n}: header is 16 + 6n', data_header_size(n) == 16 + 6 * n)
        ok &= _check(f'nCh={n}: CRC verifies', verify_data_crc(chunk, hdr))
        ok &= _check(f'nCh={n}: keyframe tail reads back',
                     hdr.keyframe == [100 + i for i in range(n)])
        sc = build_schema_chunk(s, 0)
        ok &= _check(f'nCh={n}: SCHEMA chunk is 8 + 4n + 2', len(sc) == schema_chunk_size(n))

    print('count = 1 is legal (§3.3)')
    enc = BlockEncoder(schema, 60)
    enc.reset(1000, [1234])
    chunk = enc.seal(H5_FLAG_PARTIAL)
    hdr = parse_data_header(chunk)
    ok &= _check('payloadLen == 0', hdr.payload_len == 0)
    ok &= _check('decodes to the keyframe alone',
                 decode_block(chunk, schema) == [(1000, [1234])])

    print('rejection (§3.7)')
    enc = BlockEncoder(schema, 60)
    enc.reset(1000, [10])
    enc.add(1060, [11])
    chunk = bytearray(enc.seal())
    chunk[20] ^= 0xFF                       # corrupt a tail byte
    seen = []
    list(scan(bytes(build_schema_chunk(schema, 0)) + bytes(chunk),
              lambda o, m: seen.append(m)))
    ok &= _check('corrupt DATA is skipped with a reason', seen == ['DATA CRC'], f'{seen}')

    good = build_schema_chunk(schema, 0)
    bad = bytearray(good)
    bad[8 + 4 * len(schema)] ^= 0x01        # corrupt the SCHEMA CRC
    seen = []
    list(scan(bytes(bad), lambda o, m: seen.append(m)))
    ok &= _check('corrupt SCHEMA is skipped', seen == ['SCHEMA CRC'], f'{seen}')

    ok &= _check('DATA before SCHEMA yields no series',
                 not list(read_series(bytes(chunk), 60, lambda o, m: None)))

    print('mid-day schema change (R5)')
    s1 = [ChannelDesc(0, H5_KIND_TEMP_C, -2), ChannelDesc(1, H5_KIND_TEMP_C, -2)]
    s2 = [ChannelDesc(0, H5_KIND_TEMP_C, -2), ChannelDesc(1, H5_KIND_TEMP_C, -2),
          ChannelDesc(2, H5_KIND_HUM_PCT, -1)]
    fw = FileWriter(s1, 60)
    for i in range(5):
        fw.append(1000 + 60 * i, [2000 + i, 2100 + i])
    fw.new_schema(s2)
    for i in range(5, 10):
        fw.append(1000 + 60 * i, [2000 + i, 2100 + i, 700 + i])
    blob = fw.close()
    seen_schemas = [e.schema for e in scan(blob) if e.kind == 'schema']
    ok &= _check('two SCHEMA chunks in one file', len(seen_schemas) == 2)
    rows = list(read_series(blob))
    ok &= _check('records keep the schema in force at their position',
                 [len(v) for _, _, v in rows] == [2] * 5 + [3] * 5)
    fw2 = FileWriter(s1, 60)
    fw2.append(1000, [1, 2])
    fw2.new_schema(list(s1))                 # §14-7
    ok &= _check('an identical SCHEMA is not written twice',
                 len([e for e in scan(fw2.close()) if e.kind == 'schema']) == 1)

    print('property: decode(encode(s)) == s')
    rnd = random.Random(20260731)
    trials = args.trials
    bad_trials = 0
    for t in range(trials):
        n = rnd.randint(1, H5_MAX_CHANNELS)
        s = [ChannelDesc(i, rnd.choice(list(KIND_UNIT)), rnd.randint(-3, 1))
             for i in range(n)]
        count = rnd.randint(1, H5_BLOCK_MAX_RECORDS)
        mode = rnd.choice(('ramp', 'step', 'noise', 'flat', 'nan'))
        enc = BlockEncoder(s, rnd.choice((30, 60, 300)))
        vals = [rnd.randint(-32767, 32767) for _ in range(n)]
        epoch = rnd.randint(1_600_000_000, 1_900_000_000)
        enc.reset(epoch, vals)
        expect = [(epoch, list(vals))]
        cur = list(vals)
        for i in range(1, count):
            if mode == 'ramp':
                cur = [max(-32767, min(32767, v + rnd.randint(-3, 3))) for v in cur]
            elif mode == 'step':
                cur = [max(-32767, min(32767, v + rnd.choice((0, 0, 0, 900))))
                       for v in cur]
            elif mode == 'noise':
                cur = [rnd.randint(-32767, 32767) for _ in cur]
            elif mode == 'nan':
                cur = [H5_NAN if rnd.random() < 0.3 else
                       (rnd.randint(-32767, 32767) if v == H5_NAN else
                        max(-32767, min(32767, v + rnd.randint(-40, 40))))
                       for v in cur]
            epoch += rnd.choice((60, 60, 60, 59, 61, 120, 3600, 100000))
            if not enc.add(epoch, cur):
                break               # out of RAW's reach: the block closes here
            expect.append((epoch, list(cur)))
        chunk = enc.seal()
        hdr = parse_data_header(chunk)
        if not verify_data_crc(chunk, hdr):
            bad_trials += 1
            continue
        got = decode_block(chunk, s, enc.nominal)
        if got != expect:
            bad_trials += 1
            if bad_trials <= 2:
                print(f'    mismatch n={n} count={count} mode={mode}')
    ok &= _check(f'{trials} random series roundtrip', bad_trials == 0,
                 f'{bad_trials} failures')

    print()
    print('SELFTEST', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


# ===========================================================================

def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--selftest', action='store_true')
    p.add_argument('--trials', type=int, default=20000,
                   help='random series for the property test (default 20000)')
    p.add_argument('--convert', nargs=2, metavar=('IN.bin', 'OUT.h5'))
    p.add_argument('--convert-v4', nargs=2, metavar=('IN.sim4', 'OUT.h5'))
    p.add_argument('--dump-csv', metavar='FILE.h5')
    p.add_argument('--out', metavar='FILE.csv', help='write CSV here instead of stdout')
    p.add_argument('--stats', nargs='+', metavar='FILE.h5')
    p.add_argument('--synth', metavar='OUT',
                   help='file for one day, or a directory when --days > 1')
    p.add_argument('--days', type=int, default=1)
    p.add_argument('--schema-from', metavar='FILE.h5',
                   help="take the channel set from a real device file")
    p.add_argument('--records', type=int, default=1440)
    p.add_argument('--epoch', type=int, default=1785456000)
    p.add_argument('--seed', type=int, default=42)
    p.add_argument('--dropouts', action='store_true')
    p.add_argument('--interval', type=int, default=60,
                   help='nominal sampling interval in seconds (default 60)')
    args = p.parse_args(argv)

    if args.selftest:
        return cmd_selftest(args)
    if args.convert:
        return cmd_convert(args)
    if args.convert_v4:
        return cmd_convert_v4(args)
    if args.dump_csv:
        return cmd_dump_csv(args)
    if args.stats:
        return cmd_stats(args)
    if args.synth:
        return cmd_synth(args)
    p.print_help()
    return 1


if __name__ == '__main__':
    sys.exit(main())
