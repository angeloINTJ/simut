#!/usr/bin/env python3
"""Merge V5 history day files (same day, same schema) into one file.

Why this exists: every OTA apply reformats the LittleFS (the staging region IS
the filesystem partition — src/ota/applier.cpp), so a bench that stages
firmware destroys /history, including the current-day file the device keeps
writing to. Restoring that day from a backup cannot be a plain upload: by the
time the restore runs, the device has already created a fresh file for the
same day and is appending to it. The correct operation is a MERGE of the two
versions — which is what this tool does, and what the 2026-08-21 00:00-00:22
data hole happened for lack of (docs/analysis/ANALISE_BURACO_HISTORICO_BANCADA_OTA.md).

Guarantees:
  - byte-preserving: DATA chunks are copied verbatim (CRCs untouched);
  - chronological output: blocks sorted by t0 (stable);
  - exact duplicates collapse to one copy, so merging is idempotent;
  - a chunk that fails CRC/structure aborts the merge unless --skip-bad;
  - files whose SCHEMA chunks differ byte-for-byte are refused — merging
    across schema changes is ambiguous and out of scope (§3.7-2 files with
    more than one SCHEMA chunk are refused for the same reason).

Conflict policy (two blocks with the same t0 but different content) is
`fail` by default; `--on-conflict keep-longer` keeps the block with more
records (equal-count conflicts always fail — there is no honest tiebreak).

The CLI also refuses inputs whose basenames name different days
(20260820.h5 + 20260821.h5): a merged file can only live under ONE day name,
and the firmware's per-day window would discard the other day's blocks at
seeding. --force-cross-day overrides for deliberate repair work.

Usage:
  python3 tools/h5_day_merge.py OUT.h5 IN1.h5 IN2.h5 [IN3.h5 ...]
          [--on-conflict fail|keep-longer] [--skip-bad] [--quiet]

Exit codes: 0 merged; 2 refused (schema mismatch, conflict, bad chunk, empty
input). Importable: merge_blobs() is pure and is what tools/fsguard.py and
tools/test_h5_day_merge.py use.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import history_v5 as h5  # noqa: E402


class MergeError(Exception):
    """Refusal with a reason meant for the operator."""


@dataclass
class MergeStats:
    files: int = 0
    blocks_in: int = 0
    blocks_out: int = 0
    duplicates: int = 0
    conflicts_resolved: int = 0
    bad_chunks: int = 0


def _scan_file(blob: bytes, label: str, skip_bad: bool, stats: MergeStats):
    """Return (schema_chunk_bytes, [(t0, count, crc, raw_chunk), ...])."""
    errors: list[tuple[int, str]] = []
    schema_raw = None
    blocks = []
    for e in h5.scan(blob, on_error=lambda off, msg: errors.append((off, msg))):
        if e.kind == 'schema':
            if schema_raw is not None:
                raise MergeError(
                    f'{label}: more than one SCHEMA chunk (offset {e.offset}) — '
                    f'mid-day schema changes are out of scope for merging')
            schema_raw = bytes(blob[e.offset:e.offset + e.size])
        else:
            hdr = e.header
            blocks.append((hdr.t0, hdr.count, hdr.crc,
                           bytes(blob[e.offset:e.offset + e.size])))
    if errors:
        stats.bad_chunks += len(errors)
        if not skip_bad:
            listing = '; '.join(f'offset {o}: {m}' for o, m in errors[:4])
            raise MergeError(f'{label}: {len(errors)} unreadable chunk(s) '
                             f'({listing}) — rerun with --skip-bad to drop them')
    if schema_raw is None:
        raise MergeError(f'{label}: no valid SCHEMA chunk — not a V5 day file '
                         f'(empty or corrupt download?)')
    return schema_raw, blocks


def wip_info(chunk: bytes) -> 'h5.DataHeader':
    """Validate a bare `/history/.wip` and return its DataHeader.

    On disk the .wip is exactly ONE DATA chunk, no SCHEMA (the firmware's
    recoverWipV5 adopts it verbatim into the day file at boot). A backup taken
    minutes before an OTA holds up to 59 unsealed records in it — restoring
    without absorbing the .wip loses them, which is precisely how the
    2026-08-21 hole lost most of its records. Never upload a .wip raw: a stale
    one would be adopted at the next boot as if current. Wrap it instead:
    schema_chunk + wip = a mergeable one-block day file.
    """
    entries: list = []
    errs: list = []
    for e in h5.scan(chunk, on_error=lambda off, msg: errs.append((off, msg))):
        entries.append(e)
    if errs or len(entries) != 1 or entries[0].kind != 'data' \
            or entries[0].size != len(chunk):
        raise MergeError('not a bare .wip DATA chunk '
                         f'({len(errs)} error(s), {len(entries)} entrie(s))')
    return entries[0].header


def merge_blobs(blobs, labels=None, on_conflict='fail', skip_bad=False):
    """Merge day-file blobs. Returns (merged_bytes, MergeStats).

    Raises MergeError on refusal. Pure: no I/O.
    """
    if len(blobs) < 1:
        raise MergeError('nothing to merge')
    if on_conflict not in ('fail', 'keep-longer'):
        raise MergeError(f'unknown on_conflict policy: {on_conflict!r}')
    labels = labels or [f'input #{i + 1}' for i in range(len(blobs))]
    stats = MergeStats(files=len(blobs))

    schema_raw = None
    by_t0: dict[int, tuple[int, int, bytes, str]] = {}   # t0 -> (count, crc, raw, label)
    for blob, label in zip(blobs, labels):
        sraw, blocks = _scan_file(blob, label, skip_bad, stats)
        if schema_raw is None:
            schema_raw = sraw
        elif sraw != schema_raw:
            raise MergeError(
                f'{label}: SCHEMA differs from {labels[0]} — refusing to merge '
                f'across schemas (channel set or seq changed)')
        for t0, count, crc, raw in blocks:
            stats.blocks_in += 1
            prev = by_t0.get(t0)
            if prev is None:
                by_t0[t0] = (count, crc, raw, label)
                continue
            pcount, pcrc, praw, plabel = prev
            if raw == praw:
                stats.duplicates += 1
                continue
            # Same t0, different content: a real conflict.
            if on_conflict == 'fail' or count == pcount:
                raise MergeError(
                    f'conflict at t0={t0}: {plabel} has {pcount} record(s) '
                    f'(crc {pcrc:04X}), {label} has {count} (crc {crc:04X})'
                    + ('' if on_conflict == 'fail'
                       else ' — equal length, no honest tiebreak'))
            stats.conflicts_resolved += 1
            if count > pcount:
                by_t0[t0] = (count, crc, raw, label)

    out = bytearray(schema_raw)
    for t0 in sorted(by_t0):
        out += by_t0[t0][2]
        stats.blocks_out += 1
    return bytes(out), stats


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('out')
    ap.add_argument('inputs', nargs='+')
    ap.add_argument('--on-conflict', choices=('fail', 'keep-longer'),
                    default='fail')
    ap.add_argument('--skip-bad', action='store_true')
    ap.add_argument('--force-cross-day', action='store_true')
    ap.add_argument('--quiet', action='store_true')
    args = ap.parse_args(argv)

    days = {m.group(1) for m in
            (re.fullmatch(r'(\d{8})\.h5', os.path.basename(p)) for p in args.inputs)
            if m}
    if len(days) > 1 and not args.force_cross_day:
        print(f'[h5merge] REFUSED: inputs name different days ({sorted(days)}) — '
              f'a merged file lives under one day; --force-cross-day overrides',
              file=sys.stderr)
        return 2

    blobs = []
    for path in args.inputs:
        with open(path, 'rb') as fh:
            blobs.append(fh.read())
    try:
        merged, st = merge_blobs(blobs, labels=args.inputs,
                                 on_conflict=args.on_conflict,
                                 skip_bad=args.skip_bad)
    except MergeError as exc:
        print(f'[h5merge] REFUSED: {exc}', file=sys.stderr)
        return 2
    with open(args.out, 'wb') as fh:
        fh.write(merged)
    if not args.quiet:
        print(f'[h5merge] {st.files} file(s), {st.blocks_in} block(s) in -> '
              f'{st.blocks_out} out ({st.duplicates} duplicate(s), '
              f'{st.conflicts_resolved} conflict(s) resolved, '
              f'{st.bad_chunks} bad chunk(s) dropped) -> {args.out} '
              f'({len(merged)} B)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
