#!/usr/bin/env python3
"""Generate a varied synthetic /history for bench work.

The generator in history_v5.py draws one shape — a diurnal sine with noise —
which exercises the codec but makes every graph in the UI look the same. This
one builds a catalogue of physically plausible behaviours per quantity, so the
chart pages have something worth looking at: cycling fridges, defrost ramps,
weather fronts, stuck sensors, saturation plateaus, outage gaps.

Each channel of each day picks a shape appropriate to its kind, and each day
picks a scenario that can also punch holes in the record (outages, reboots, a
sensor going offline for hours). Data stays inside plausible physical ranges,
because a graph that reads -40 °C in a living room teaches nothing about the
rendering.

The schema is taken from a real device file so the channel ids match what the
device has provisioned — a mismatch there is the "frozen schema" trap, where
history silently records empty values.

Usage:
    gen_synth_history.py --schema-from FILE.h5 --out DIR \\
                         --end-date 2026-08-14 --days 60 [--seed N]
    gen_synth_history.py ... --target-bytes 737935   # stop at a disk budget
"""

import argparse
import datetime as dt
import math
import os
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from history_v5 import (ChannelDesc, H5_KIND_HUM_PCT, H5_KIND_PRESS_HPA,
                        H5_KIND_TEMP_C, H5_NAN, _write_series,
                        schema_from_file)

LFS_BLOCK = 4096
LFS_META_BLOCKS = 0          # measured empirically; see --report

# ---------------------------------------------------------------------------
# shapes — each returns a value for sample i of n, in real units
# ---------------------------------------------------------------------------


def _phase(i, n):
    return 2 * math.pi * i / n


def diurnal(base, swing, peak_frac=0.6):
    """Ordinary day: one warm peak in the afternoon."""
    def f(i, n, rnd):
        return base + swing * math.sin(_phase(i, n) - math.pi / 2
                                       + 2 * math.pi * (peak_frac - 0.5))
    return f


def cycling(low, high, period_min, duty=0.45):
    """A fridge or an air conditioner: pulls down, drifts up, repeats."""
    def f(i, n, rnd):
        p = (i % period_min) / period_min
        if p < duty:                       # compressor on, pulling down
            return high - (high - low) * (p / duty)
        return low + (high - low) * ((p - duty) / (1 - duty)) ** 0.8
    return f


def defrost(low, high, period_min, warm_min):
    """Cycling with a periodic defrost: a slow climb, then a fast recovery."""
    base = cycling(low, high, period_min)

    def f(i, n, rnd):
        v = base(i, n, rnd)
        into = i % (period_min * 8)
        if into < warm_min:                # defrost heater on
            return v + 6.0 * math.sin(math.pi * into / warm_min)
        return v
    return f


def ramp(start, end):
    def f(i, n, rnd):
        return start + (end - start) * (i / max(1, n - 1))
    return f


def step_and_recover(base, jump, at_frac, tau_min):
    """A door opened: a jump, then an exponential return to base."""
    def f(i, n, rnd):
        at = int(n * at_frac)
        if i < at:
            return base
        return base + jump * math.exp(-(i - at) / tau_min)
    return f


def flatline(value):
    """A stuck sensor: the same reading forever, which is not the same as NaN."""
    def f(i, n, rnd):
        return value
    return f


def sawtooth(low, high, period_min):
    def f(i, n, rnd):
        return low + (high - low) * ((i % period_min) / period_min)
    return f


def semidiurnal(base, amp):
    """Atmospheric tide: two maxima a day, which is what pressure really does."""
    def f(i, n, rnd):
        return base + amp * math.sin(2 * _phase(i, n) + 0.6)
    return f


def front(base, drop, at_frac, width_frac):
    """A weather front: pressure falls through it and recovers behind."""
    def f(i, n, rnd):
        x = (i / n - at_frac) / width_frac
        return base - drop * math.exp(-x * x)
    return f


def plateau(base, top, start_frac, end_frac):
    """Saturation: rises to a ceiling, sits on it, comes back down."""
    def f(i, n, rnd):
        p = i / n
        if p < start_frac:
            return base + (top - base) * (p / start_frac)
        if p < end_frac:
            return top
        return top - (top - base) * ((p - end_frac) / max(1e-6, 1 - end_frac))
    return f


# ---------------------------------------------------------------------------
# per-kind shape catalogues
# ---------------------------------------------------------------------------

def _weighted(rnd, options):
    """options: (label, fn, noise, weight). A stuck sensor is a fault, not a
    fifth of all days — uniform choice over shapes made faults look routine."""
    total = sum(o[3] for o in options)
    r = rnd.random() * total
    acc = 0.0
    for label, fn, noise, w in options:
        acc += w
        if r < acc:
            return label, fn, noise
    return options[0][:3]


def temp_shapes(rnd, role):
    """role: 'fridge' | 'room' | 'outdoor'."""
    if role == "fridge":
        return _weighted(rnd, [
            ("cycling", cycling(-19.5, -17.0, rnd.choice([28, 35, 42])), 0.05, 34),
            ("defrost", defrost(-19.0, -17.2, 34, 25), 0.05, 26),
            ("door open", step_and_recover(-18.5, 7.5, 0.42,
                                           rnd.choice([25, 40])), 0.06, 20),
            ("drift up", ramp(-19.0, -15.5), 0.05, 14),
            ("stuck", flatline(-18.2), 0.0, 6),
        ])
    if role == "outdoor":
        return _weighted(rnd, [
            ("diurnal", diurnal(18 + rnd.uniform(-4, 6), rnd.uniform(4, 9)), 0.12, 40),
            ("heat wave", diurnal(31, 5.5), 0.15, 15),
            ("cold snap", diurnal(9, 3.0), 0.10, 15),
            ("front passage", ramp(26, 14), 0.20, 18),
            ("overcast flat", diurnal(19, 1.2), 0.08, 12),
        ])
    return _weighted(rnd, [
        ("diurnal", diurnal(22 + rnd.uniform(-1.5, 2.5), rnd.uniform(1.5, 4)), 0.06, 34),
        ("hvac cycling", cycling(21.2, 23.4, rnd.choice([45, 60, 90])), 0.05, 22),
        ("slow drift", ramp(21.0, 25.5), 0.05, 15),
        ("step", step_and_recover(22.5, 3.0, 0.35, 90), 0.06, 14),
        ("night dip", diurnal(23, 3.2, peak_frac=0.55), 0.06, 12),
        ("stuck", flatline(22.4), 0.0, 3),
    ])


def hum_shapes(rnd):
    return rnd.choice([
        ("inverse diurnal", lambda i, n, r: 68 - 12 * math.sin(_phase(i, n)
                                                               - math.pi / 2), 0.5),
        ("rain saturation", plateau(62, 99.5, 0.28, 0.62), 0.4),
        ("ventilation drop", step_and_recover(78, -26, 0.4, 70), 0.5),
        ("hvac sawtooth", sawtooth(48, 63, rnd.choice([50, 75])), 0.4),
        ("dry spell", ramp(52, 31), 0.5),
        ("humid plateau", plateau(70, 88, 0.15, 0.8), 0.4),
    ])


def press_shapes(rnd):
    return rnd.choice([
        ("atmospheric tide", semidiurnal(1013.5 + rnd.uniform(-4, 4), 1.4), 0.08),
        ("storm front", front(1014.0, 13.0, 0.45, 0.16), 0.10),
        ("high pressure", semidiurnal(1024.0, 0.8), 0.06),
        ("low pressure", semidiurnal(1002.0, 1.1), 0.09),
        ("steady fall", ramp(1018.0, 1006.0), 0.08),
        ("steady rise", ramp(1004.0, 1017.0), 0.08),
    ])


# ---------------------------------------------------------------------------
# day scenarios — what happens to the record itself
# ---------------------------------------------------------------------------

SCENARIOS = [
    ("complete", 0.40),
    ("power outage", 0.14),
    ("reboot gaps", 0.14),
    ("sensor offline", 0.12),
    ("late start", 0.08),
    ("early stop", 0.06),
    ("noisy day", 0.06),
]


def pick_scenario(rnd):
    r = rnd.random()
    acc = 0.0
    for name, w in SCENARIOS:
        acc += w
        if r < acc:
            return name
    return "complete"


def build_day(schema, day_epoch, interval, rnd, roles):
    """Return (series, scenario, notes) for one day."""
    n = 86400 // interval
    scenario = pick_scenario(rnd)
    notes = []

    shapes, noises = [], []
    for d in schema:
        if d.kind == H5_KIND_TEMP_C:
            label, fn, noise = temp_shapes(rnd, roles.get(d.id, "room"))
        elif d.kind == H5_KIND_HUM_PCT:
            label, fn, noise = hum_shapes(rnd)
        elif d.kind == H5_KIND_PRESS_HPA:
            label, fn, noise = press_shapes(rnd)
        else:
            label, fn, noise = "generic", diurnal(100, 10), 1.0
        shapes.append(fn)
        noises.append(noise)
        notes.append(f"{d.label()}={label}")

    # Holes in the record, expressed as sample ranges to skip entirely.
    skip = []
    if scenario == "power outage":
        start = rnd.randrange(int(n * 0.1), int(n * 0.7))
        length = rnd.randrange(90, 420)
        skip.append((start, start + length))
        notes.append(f"outage {length} min")
    elif scenario == "reboot gaps":
        for _ in range(rnd.randrange(2, 5)):
            start = rnd.randrange(0, n - 20)
            skip.append((start, start + rnd.randrange(2, 9)))
        notes.append(f"{len(skip)} reboots")
    elif scenario == "late start":
        skip.append((0, rnd.randrange(int(n * 0.15), int(n * 0.45))))
        notes.append("device off overnight")
    elif scenario == "early stop":
        skip.append((rnd.randrange(int(n * 0.55), int(n * 0.85)), n))
        notes.append("device off from evening")

    # One channel dark for a stretch — NaN, not absence: the record exists.
    offline = None
    if scenario == "sensor offline":
        ch = rnd.randrange(len(schema))
        a = rnd.randrange(0, int(n * 0.6))
        offline = (ch, a, a + rnd.randrange(120, 480))
        notes.append(f"{schema[ch].label()} offline "
                     f"{offline[2] - offline[1]} min")

    noise_boost = 6.0 if scenario == "noisy day" else 1.0
    if scenario == "noisy day":
        notes.append("interference")

    series = []
    for i in range(n):
        if any(a <= i < b for a, b in skip):
            continue
        vals = []
        for c, d in enumerate(schema):
            if offline and offline[0] == c and offline[1] <= i < offline[2]:
                vals.append(H5_NAN)
                continue
            v = shapes[c](i, n, rnd) + rnd.gauss(0, noises[c] * noise_boost)
            # Rare isolated outliers, the kind that make a graph's envelope lie.
            if rnd.random() < 0.0006:
                v += rnd.choice([-1, 1]) * noises[c] * rnd.uniform(18, 45)
            if d.kind == H5_KIND_HUM_PCT:
                v = max(0.0, min(100.0, v))
            elif d.kind == H5_KIND_PRESS_HPA:
                v = max(870.0, min(1085.0, v))
            else:
                v = max(-55.0, min(85.0, v))
            raw = int(round(v * (10.0 ** -d.scale_exp)))
            vals.append(max(-32767, min(32767, raw)))
        series.append((day_epoch + i * interval, vals))
    return series, scenario, notes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--schema-from", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--end-date", required=True, help="YYYY-MM-DD, inclusive")
    ap.add_argument("--days", type=int, default=60)
    ap.add_argument("--interval", type=int, default=60)
    ap.add_argument("--seed", type=int, default=20260815)
    ap.add_argument("--target-bytes", type=int, default=0,
                    help="stop once projected on-disk usage reaches this")
    a = ap.parse_args()

    schema = schema_from_file(a.schema_from)
    rnd = random.Random(a.seed)
    os.makedirs(a.out, exist_ok=True)

    # Slot 0 is the plasma fridge, slot 1 the secret hideout (also cold), the
    # BMP280 sits outdoors-ish; everything else is a room.
    roles = {0: "fridge", 8: "fridge", 32: "outdoor"}

    end = dt.date.fromisoformat(a.end_date)
    disk = 0
    made = []
    # Backwards from the end date. A budget that runs out has to drop the
    # OLDEST days: the graph pages open on the last 24 h, so a set that stops a
    # month short of today renders an empty chart and looks like a bug.
    for k in range(a.days):
        day = end - dt.timedelta(days=k)
        midnight = int(dt.datetime.combine(day, dt.time()).timestamp())
        series, scenario, notes = build_day(schema, midnight, a.interval,
                                            rnd, roles)
        if not series:
            continue
        path = os.path.join(a.out, f"{day:%Y%m%d}.h5")
        count, size = _write_series(path, schema, series, a.interval)
        blocks = max(1, -(-size // LFS_BLOCK)) + LFS_META_BLOCKS
        disk += blocks * LFS_BLOCK
        made.append((f"{day:%Y%m%d}", count, size, blocks, scenario, notes))
        if a.target_bytes and disk >= a.target_bytes:
            break

    made.sort(key=lambda r: r[0])
    print(f"{len(made)} dias, {disk} B de disco projetados "
          f"({disk / LFS_BLOCK:.0f} blocos de {LFS_BLOCK} B)\n")
    for name, count, size, blocks, scenario, notes in made:
        print(f"  {name}  {count:>4d} regs  {size:>6d} B  {blocks}blk  "
              f"{scenario:<14s} {'; '.join(notes[:3])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
