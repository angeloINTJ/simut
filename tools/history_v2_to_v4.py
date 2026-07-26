#!/usr/bin/env python3
"""history_v2_to_v4.py — convert SIMUT v2/v3 history (.bin) to V4 (.sim4).

The two formats differ in more than encoding. v2/v3 is POSITIONAL: a record is
ambient temp/hum, sixteen slot temperatures, sixteen slot humidities and one
pressure, and nothing in the file says which physical sensor a slot held. V4 is
SCHEMA-DRIVEN: the header names each sensor by hwId and declares which channels
it carries, and values are matched by that identity.

A converter therefore cannot invent the destination schema. It needs a
reference .sim4 written by the device whose data is being converted — its
header is copied verbatim, so the output carries the exact hwIds, bit widths
and scales the firmware expects. Download any .sim4 from /history to get one.

    python3 tools/history_v2_to_v4.py --schema ref.sim4 in.bin out.sim4

The default mapping assumes v2 slot i corresponds to V4 sensor index i, which
holds when the slot layout did not change between the two files. It is an
assumption, not something the files can confirm — run --dry-run first: it
reports how many records each measurement would receive, and a column of zeros
is the sign the mapping is wrong. Override with --map.

    --dry-run                 report coverage without writing
    --map 0=t3,1=h3,2=p       route V4 measurement N from a v2 field
                              t<i> slot temperature, h<i> slot humidity,
                              p pressure, at ambient temp, ah ambient humidity

Formats, as implemented by HistoryCodec.cpp and HistoryV4.cpp:

  v2/v3 header  16 B: "SIM2" | version u16 | anchorPeriod u16 | flags u32 |
                      recordCount u32
  anchor        every anchorPeriod records; 40 B for version 2 (epoch, ambient
                temp/hum, 16 slot temps), 74 B for version 3 (adds 16
                humidities and pressure)
  delta         mask (3 B for v2, 5 B for v3, little-endian) | zigzag varint
                dEpoch | one zigzag varint per set mask bit, in bit order:
                0 ambient temp, 1 ambient humidity, 2..17 slot temps,
                18..33 slot humidities, 34 pressure. A field carries an
                ABSOLUTE value the first time it appears and a delta after,
                which is why decoding has to track validity per field.

  -32768 is the NaN sentinel on the v2 side; V4 uses all-ones at the field's
  bit width. Scales differ too: v2 stores temperature and humidity x100 and
  pressure x10, while V4 stores value * measure.scale, so every value is
  converted back to a real number and re-scaled rather than copied.

Project: SIMUT
License: MIT
"""

import argparse
import struct
import sys
from pathlib import Path

V2_MAGIC = b"SIM2"
V2_HEADER_SIZE = 16
V2_ANCHOR_SIZE = 40
V3_ANCHOR_SIZE = 74
MAX_SENSORS = 16
NAN_I16 = -32768

V4_MAGIC = b"SIM4"
V4_HEADER_FIXED = 16

# v2 field index -> label, matching HistoryCodecState::fieldHasValid
F_AMB_T, F_AMB_H = 0, 1
F_TEMP0, F_HUM0, F_PRESS = 2, 18, 34


# ── varint ─────────────────────────────────────────────────────────────────

def read_varint_z(buf, off):
    """Zigzag varint, as writeVarintZ/readVarintZ in HistoryCodec.cpp."""
    shift = 0
    raw = 0
    while True:
        if off >= len(buf):
            raise ValueError("varint truncated")
        b = buf[off]
        off += 1
        raw |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
        if shift > 35:
            raise ValueError("varint too long")
    val = (raw >> 1) ^ -(raw & 1)
    return val, off


# ── v2/v3 reader ───────────────────────────────────────────────────────────

class V2Record:
    __slots__ = ("epoch", "amb_t", "amb_h", "temp", "hum", "press")

    def __init__(self):
        self.epoch = 0
        self.amb_t = NAN_I16
        self.amb_h = NAN_I16
        self.temp = [NAN_I16] * MAX_SENSORS
        self.hum = [NAN_I16] * MAX_SENSORS
        self.press = NAN_I16

    def field(self, idx):
        if idx == F_AMB_T:
            return self.amb_t
        if idx == F_AMB_H:
            return self.amb_h
        if F_TEMP0 <= idx < F_TEMP0 + MAX_SENSORS:
            return self.temp[idx - F_TEMP0]
        if F_HUM0 <= idx < F_HUM0 + MAX_SENSORS:
            return self.hum[idx - F_HUM0]
        if idx == F_PRESS:
            return self.press
        raise IndexError(idx)

    def set_field(self, idx, v):
        if idx == F_AMB_T:
            self.amb_t = v
        elif idx == F_AMB_H:
            self.amb_h = v
        elif F_TEMP0 <= idx < F_TEMP0 + MAX_SENSORS:
            self.temp[idx - F_TEMP0] = v
        elif F_HUM0 <= idx < F_HUM0 + MAX_SENSORS:
            self.hum[idx - F_HUM0] = v
        elif idx == F_PRESS:
            self.press = v
        else:
            raise IndexError(idx)


def read_v2(path):
    """Yield V2Record in file order. Returns (version, [records])."""
    data = Path(path).read_bytes()
    if len(data) < V2_HEADER_SIZE or data[:4] != V2_MAGIC:
        sys.exit(f"{path}: not a SIM2 file (magic {data[:4]!r})")
    _, version, anchor_period, _flags, _count = struct.unpack_from("<4sHHII", data, 0)
    if anchor_period == 0:
        sys.exit(f"{path}: anchorPeriod is 0")

    off = V2_HEADER_SIZE
    n = 0
    last = V2Record()
    has_valid = [False] * 35
    out = []

    while off < len(data):
        is_anchor = (n % anchor_period) == 0
        rec = V2Record()

        if is_anchor:
            # The version field says v2 or v3, but the anchor size is what the
            # decoder actually trusts (historyDecodeRecord auto-detects), so
            # honour the declared version and verify the bytes are there.
            size = V3_ANCHOR_SIZE if version >= 3 else V2_ANCHOR_SIZE
            if off + size > len(data):
                break  # truncated tail — keep what decoded cleanly
            rec.epoch, rec.amb_t, rec.amb_h = struct.unpack_from("<Ihh", data, off)
            rec.temp = list(struct.unpack_from("<16h", data, off + 8))
            if version >= 3:
                rec.hum = list(struct.unpack_from("<16h", data, off + 40))
                rec.press, = struct.unpack_from("<h", data, off + 72)
            off += size
        else:
            mask_bytes = 5 if version >= 3 else 3
            if off + mask_bytes > len(data):
                break
            mask = int.from_bytes(data[off:off + mask_bytes], "little")
            off += mask_bytes
            try:
                depoch, off = read_varint_z(data, off)
                rec.epoch = (last.epoch + depoch) & 0xFFFFFFFF
                nfields = 35 if version >= 3 else 18
                for f in range(nfields):
                    if not (mask >> f) & 1:
                        continue
                    d, off = read_varint_z(data, off)
                    v = (last.field(f) + d) if has_valid[f] else d
                    # int16 wrap, as the C++ cast does
                    v = ((v + 32768) & 0xFFFF) - 32768
                    rec.set_field(f, v)
            except ValueError:
                break  # truncated tail

        # Validity tracks the LAST ANCHOR-OR-DELTA value per field, exactly as
        # updateFieldValidityV2/V3 and the delta branch do.
        for f in range(35):
            v = rec.field(f)
            if is_anchor:
                has_valid[f] = (v != NAN_I16)
            elif v != NAN_I16:
                has_valid[f] = True
        merged = V2Record()
        merged.epoch = rec.epoch
        for f in range(35):
            v = rec.field(f)
            merged.set_field(f, v if v != NAN_I16 else NAN_I16)
        last_epoch_holder = last
        last = V2Record()
        last.epoch = rec.epoch
        for f in range(35):
            v = rec.field(f)
            last.set_field(f, v if v != NAN_I16 else last_epoch_holder.field(f))

        out.append(merged)
        n += 1

    return version, out


# ── V4 schema (read from a reference file) ─────────────────────────────────

class V4Measure:
    __slots__ = ("sensor_idx", "channel", "bit_width", "decimals", "scale", "hw_id")


def read_v4_schema(path):
    data = Path(path).read_bytes()
    if len(data) < V4_HEADER_FIXED or data[:4] != V4_MAGIC:
        sys.exit(f"{path}: not a SIM4 file (magic {data[:4]!r})")
    (_, version, header_size, anchor_period,
     sensor_count, measure_count, _flags, strpool_size, _res) = struct.unpack_from(
        "<4sHHHBBBBH", data, 0)
    if header_size > len(data):
        sys.exit(f"{path}: header claims {header_size} B, file has {len(data)}")

    off = V4_HEADER_FIXED
    sensors = []
    for _ in range(sensor_count):
        hw_off, hw_len, nm_off, nm_len, stype, chmask, _fl, _r0, _r1 = struct.unpack_from(
            "<9B", data, off)
        sensors.append((hw_off, hw_len))
        off += 9
    measures = []
    for _ in range(measure_count):
        # 12 bytes, not 10: the six uint8 are followed by two padding bytes
        # before the uint32 scale. histV4ReadHeaderBuf memcpy's sizeof(struct)
        # straight out of the file, so the on-disk stride is whatever the
        # compiler laid out — measured, not assumed.
        s_idx, ch, bw, dec, u_off, u_len, scale = struct.unpack_from("<6B2xI", data, off)
        m = V4Measure()
        m.sensor_idx, m.channel, m.bit_width, m.decimals, m.scale = s_idx, ch, bw, dec, scale
        measures.append(m)
        off += 12
    pool = data[off:off + strpool_size]
    for m in measures:
        if m.sensor_idx < len(sensors):
            o, l = sensors[m.sensor_idx]
            m.hw_id = pool[o:o + l].decode("utf-8", "replace")
        else:
            m.hw_id = "?"
    return data[:header_size], anchor_period, measures


# ── V4 writer ──────────────────────────────────────────────────────────────

def v4_nan(bit_width):
    return (1 << bit_width) - 1 if bit_width < 64 else (1 << 64) - 1


class BitWriter:
    """Writes anchor records, which are bit-packed: 32-bit epoch followed by
    each measurement at its declared bit width, in table order."""

    def __init__(self):
        self.buf = bytearray()
        self.bit = 0

    def put(self, value, width):
        for i in range(width):
            if self.bit % 8 == 0:
                self.buf.append(0)
            if (value >> i) & 1:
                self.buf[self.bit // 8] |= 1 << (self.bit % 8)
            self.bit += 1

    def bytes(self):
        return bytes(self.buf)


def write_varint_z(out, v):
    u = ((v << 1) ^ (v >> 31)) & 0xFFFFFFFF
    while True:
        b = u & 0x7F
        u >>= 7
        if u:
            out.append(b | 0x80)
        else:
            out.append(b)
            return


# ── conversion ─────────────────────────────────────────────────────────────

FIELD_NAMES = {"at": F_AMB_T, "ah": F_AMB_H, "p": F_PRESS}


def parse_field(token):
    token = token.strip().lower()
    if token in FIELD_NAMES:
        return FIELD_NAMES[token]
    if token.startswith("t") and token[1:].isdigit():
        return F_TEMP0 + int(token[1:])
    if token.startswith("h") and token[1:].isdigit():
        return F_HUM0 + int(token[1:])
    sys.exit(f"unknown v2 field: {token!r} (use t<i>, h<i>, p, at, ah)")


def default_map(measures):
    """V4 measurement -> v2 field, assuming sensor index i == v2 slot i."""
    m = {}
    for i, meas in enumerate(measures):
        if meas.channel == 0:
            m[i] = F_TEMP0 + meas.sensor_idx
        elif meas.channel == 1:
            m[i] = F_HUM0 + meas.sensor_idx
        elif meas.channel == 2:
            m[i] = F_PRESS
    return m


def convert(schema_path, src, dst, mapping_arg, dry_run):
    header, anchor_period, measures = read_v4_schema(schema_path)
    version, records = read_v2(src)
    if not records:
        sys.exit(f"{src}: no records decoded")

    mapping = default_map(measures)
    if mapping_arg:
        for pair in mapping_arg.split(","):
            k, _, v = pair.partition("=")
            if not v:
                sys.exit(f"bad --map entry: {pair!r} (expected N=field)")
            idx = int(k)
            if idx >= len(measures):
                sys.exit(f"--map {pair}: schema has {len(measures)} measurements")
            mapping[idx] = parse_field(v)

    print(f"source : {src}  version {version}, {len(records)} records")
    print(f"schema : {schema_path}  {len(measures)} measurements, anchorPeriod {anchor_period}")
    print(f"span   : {records[0].epoch} .. {records[-1].epoch}")
    print()
    print("  V4 measurement          <- v2 field    records with data")
    covered = 0
    for i, meas in enumerate(measures):
        f = mapping.get(i)
        label = {0: "temp", 1: "hum", 2: "press"}.get(meas.channel, f"ch{meas.channel}")
        src_name = "(unmapped)" if f is None else v2_field_name(f)
        n = 0 if f is None else sum(1 for r in records if r.field(f) != NAN_I16)
        covered += n
        flag = ""
        if f is not None and n == 0:
            flag = "  <-- empty"
        elif f is not None:
            # v2 keeps two decimals for temperature and humidity and one for
            # pressure. When the destination measurement declares a coarser
            # scale the values are quantised on the way in, and that is a
            # property of the target schema, not a conversion fault — say so
            # rather than let someone discover it comparing graphs later.
            src_scale = 10 if f == F_PRESS else 100
            if meas.scale < src_scale:
                flag = f"  <-- quantised to 1/{meas.scale}"
        print(f"  {i:2d} {meas.hw_id:<10s} {label:<6s} <- {src_name:<10s} {n:6d}{flag}")
    if covered == 0:
        sys.exit("\nno measurement received a single value — the mapping is wrong")
    if dry_run:
        print("\n--dry-run: nothing written")
        return

    out = bytearray(header)
    last_anchor = [None] * len(measures)
    last_epoch = 0

    for n, rec in enumerate(records):
        vals = []
        for i, meas in enumerate(measures):
            f = mapping.get(i)
            raw = NAN_I16 if f is None else rec.field(f)
            if raw == NAN_I16:
                vals.append(None)
            else:
                # v2 scale is fixed (x100, or x10 for pressure); V4 carries its
                # own per-measurement scale, so go through the real value.
                real = raw / (10.0 if f == F_PRESS else 100.0)
                v = int(round(real * meas.scale))
                lo, hi = v4_range(meas)
                vals.append(max(lo, min(hi, v)))

        if n % anchor_period == 0:
            bw = BitWriter()
            bw.put(rec.epoch & 0xFFFFFFFF, 32)
            for i, meas in enumerate(measures):
                v = vals[i]
                bw.put(v4_nan(meas.bit_width) if v is None else (v & ((1 << meas.bit_width) - 1)),
                       meas.bit_width)
            out += bw.bytes()
            last_anchor = list(vals)
        else:
            mask = 0
            payload = bytearray()
            for i, v in enumerate(vals):
                if v is not None:
                    mask |= 1 << i
            body = bytearray()
            write_varint_z(body, rec.epoch - last_epoch)
            for i, v in enumerate(vals):
                if v is None:
                    continue
                base = last_anchor[i]
                write_varint_z(body, v if base is None else v - base)
                last_anchor[i] = v
            mask_bytes = (len(measures) + 7) // 8
            out += mask.to_bytes(mask_bytes, "little") + body
        last_epoch = rec.epoch

    Path(dst).write_bytes(out)
    print(f"\nwrote {dst}: {len(out)} B, {len(records)} records")


def v4_range(meas):
    nan = v4_nan(meas.bit_width)
    hi = nan - 1
    if meas.channel == 0:  # signed
        return -(hi // 2) - 1, hi // 2
    return 0, hi


def v2_field_name(f):
    if f == F_AMB_T:
        return "amb temp"
    if f == F_AMB_H:
        return "amb hum"
    if f == F_PRESS:
        return "pressure"
    if F_TEMP0 <= f < F_TEMP0 + MAX_SENSORS:
        return f"t{f - F_TEMP0}"
    return f"h{f - F_HUM0}"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--schema", required=True, metavar="ref.sim4",
                    help="reference .sim4 whose header supplies the destination schema")
    ap.add_argument("src", help="input .bin (SIM2)")
    ap.add_argument("dst", nargs="?", help="output .sim4")
    ap.add_argument("--map", metavar="N=field,...", help="route V4 measurement N from a v2 field")
    ap.add_argument("--dry-run", action="store_true", help="report coverage, write nothing")
    a = ap.parse_args()
    if not a.dry_run and not a.dst:
        ap.error("dst is required unless --dry-run")
    convert(a.schema, a.src, a.dst, a.map, a.dry_run)


if __name__ == "__main__":
    main()
