#!/usr/bin/env python3
"""LittleFS guard for OTA benches: backup and restore with day-file merging.

Every OTA apply reformats the LittleFS (staging IS the filesystem partition,
src/ota/applier.cpp), so any bench that stages firmware MUST wrap the battery:

    SIMUT_WEB_USER=admin SIMUT_WEB_PASS=... \
    python3 tools/fsguard.py backup  --host 192.168.3.24 --out  bak/
    ... stage/apply cycles ...
    python3 tools/fsguard.py restore --host 192.168.3.24 --from bak/

What this fixes (the 2026-08-21 00:00-00:22 history hole, see
docs/analysis/ANALISE_BURACO_HISTORICO_BANCADA_OTA.md): earlier ad-hoc guards
skipped the CURRENT-DAY history file on both sides — never backed up, never
restored — because the device keeps writing to it and a plain re-upload would
clash. The rule here instead:

  - backup copies /history COMPLETE, current day AND the bare `.wip`
    included — outside the top of the hour the .wip is the ONLY carrier of
    the still-unsealed block (up to 59 records);
  - restore uploads what the device lacks, skips what already matches, and
    for a .h5 present on BOTH sides with different content it MERGES the two
    versions (tools/h5_day_merge.py: chronological, byte-preserving,
    duplicate-collapsing) and uploads the merge — retrying if the device
    seals a new block mid-operation;
  - the backed-up `.wip` is never uploaded as a file (the next boot would
    adopt a stale snapshot): it is absorbed into its day's file as one more
    DATA block before that file is compared/merged — UNLESS the device still
    owns that block (same t0 live in the device's .wip, or already sealed in
    its day file), which is what a restore without an intervening wipe looks
    like; absorbing then would duplicate records, so it is skipped.

Known device warts baked in: /api/ls rate-limits ("Too Fast") AND returns an
empty listing while the heavy-task lock is held — both are retried, never
trusted on first answer; README.txt / system.bin / system.blog are never
restored (READMEs drop the connection on overwrite; the blog is the device's
own record and must not be forged back).

Residual risk, by design: records written between `backup` and the first
post-OTA boot (~1.5-2 min for one stage+apply) exist nowhere and cannot be
restored. Run `backup` as late as possible, immediately before the stage.

Credentials come from SIMUT_WEB_USER / SIMUT_WEB_PASS (same convention as
tools/web_test_suite.py) or --user/--password. Exit code 0 = clean, 1 = at
least one file failed, 2 = bad invocation/login.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import time
from pathlib import Path

import requests

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import history_v5 as h5  # noqa: E402
from h5_day_merge import MergeError, merge_blobs, wip_info  # noqa: E402

LS_SPACING_S = 2.6          # /api/ls rate limit ("Too Fast" under ~2.5 s)
LS_ATTEMPTS = 5             # empty listing = heavy-task lock, not truth
DL_SPACING_S = 0.4
MERGE_ATTEMPTS = 3          # device may seal a block mid-merge; re-merge
NEVER_RESTORE = {'README.txt', 'system.bin', 'system.blog', 'system.old.blog'}


def sha256(blob: bytes) -> str:
    return hashlib.sha256(blob).hexdigest()


class Rig:
    def __init__(self, host: str, user: str, password: str):
        self.base = f'http://{host}'
        self.s = requests.Session()
        n = self.s.get(f'{self.base}/api/login_init', timeout=10).json().get('nonce', '')
        r = self.s.post(f'{self.base}/api/login', data={
            'user': user,
            'pass': hashlib.sha256(password.encode('latin-1')).hexdigest(),
            'nonce': n}, timeout=10)
        if r.status_code not in (200, 302) or 'SIMUTSESS' not in self.s.cookies.get_dict():
            raise SystemExit(2)
        self._last_ls = 0.0

    def ls(self, d: str) -> list[dict]:
        for attempt in range(LS_ATTEMPTS):
            wait = LS_SPACING_S - (time.time() - self._last_ls)
            if wait > 0:
                time.sleep(wait)
            self._last_ls = time.time()
            j = self.s.get(f'{self.base}/api/ls', params={'dir': d}, timeout=15).json()
            entries = j.get('entries')
            if 'error' not in j and entries:
                return entries
            if 'error' not in j and entries == [] and attempt >= 1:
                return []          # twice-confirmed empty is believable
        raise RuntimeError(f'ls {d}: still empty/rate-limited after {LS_ATTEMPTS} tries')

    def download(self, path: str) -> bytes | None:
        time.sleep(DL_SPACING_S)
        r = self.s.get(f'{self.base}/download', params={'file': path}, timeout=60)
        if r.status_code == 404:
            return None
        r.raise_for_status()
        return r.content

    def upload(self, remote_dir: str, name: str, blob: bytes) -> None:
        time.sleep(DL_SPACING_S)
        r = self.s.post(f'{self.base}/api/upload', params={'uploadDir': remote_dir},
                        files={'file': (name, blob, 'application/octet-stream')},
                        timeout=90)
        r.raise_for_status()


def cmd_backup(rig: Rig, out: Path, dirs: list[str]) -> int:
    out.mkdir(parents=True, exist_ok=True)
    manifest = {'host': rig.base, 'created': time.strftime('%Y-%m-%dT%H:%M:%S'),
                'dirs': dirs, 'files': []}
    failed = 0
    for d in dirs:
        local_dir = out / d.strip('/').replace('/', '_')
        local_dir.mkdir(parents=True, exist_ok=True)
        for e in sorted(rig.ls(d), key=lambda x: x.get('n', '')):
            if e.get('t') != 'f':
                continue
            name, size = e['n'], int(e.get('s', -1))
            blob = rig.download(f'{d.rstrip("/")}/{name}')
            live = name == '.wip' or name.endswith('.h5')  # device appends between ls and GET
            if blob is None or (size >= 0 and len(blob) != size and not live):
                got = 'absent' if blob is None else f'{len(blob)} B'
                print(f'[fsguard] FAIL  {d}/{name}: expected {size} B, got {got}')
                failed += 1
                continue
            (local_dir / name).write_bytes(blob)
            manifest['files'].append({'dir': d, 'name': name, 'local': f'{local_dir.name}/{name}',
                                      'size': len(blob), 'sha256': sha256(blob)})
            print(f'[fsguard] saved {d}/{name} ({len(blob)} B)')
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=1))
    print(f'[fsguard] backup: {len(manifest["files"])} file(s), {failed} failure(s) '
          f'-> {out / "manifest.json"}')
    return 1 if failed else 0


def _restore_merge(rig: Rig, remote: str, remote_dir: str, name: str,
                   bak_blob: bytes, dev_blob: bytes) -> str:
    """Device and backup both have the file, contents differ: merge them."""
    for attempt in range(MERGE_ATTEMPTS):
        merged, st = merge_blobs([dev_blob, bak_blob], labels=['device', 'backup'])
        if merged == dev_blob:
            return f'device already superset ({st.blocks_out} blocks)'
        rig.upload(remote_dir, name, merged)
        back = rig.download(remote) or b''
        if back == merged or back.startswith(merged):
            # startswith: the device appended a freshly sealed block after our
            # upload — that is the file working as intended, not a failure.
            return (f'merged {st.blocks_in}->{st.blocks_out} blocks '
                    f'({st.duplicates} dup)')
        dev_blob = back      # a block sealed between download and upload; redo
    raise RuntimeError(f'{remote}: still changing after {MERGE_ATTEMPTS} merge attempts')


def _schema_chunk_of(blob: bytes) -> bytes:
    first = next(iter(h5.scan(blob)), None)
    if first is None or first.kind != 'schema':
        raise MergeError('no SCHEMA chunk at file start')
    return bytes(blob[first.offset:first.offset + first.size])


def _load_backup(src: Path, manifest: dict):
    """Read + sha-verify the backup; absorb any bare .wip into its day file.

    Returns ({(dir, name): blob}, {(dir, dayname): wip_chunk_pending_schema},
    failures). A .wip is never restored as a file: it is one unsealed DATA
    block, and uploading it raw would make the next boot adopt a stale
    snapshot. Merged into its day it is just one more block.
    """
    blobs: dict[tuple[str, str], bytes] = {}
    pending: dict[tuple[str, str], bytes] = {}
    failed = 0
    for f in manifest['files']:
        if f['name'] in NEVER_RESTORE:
            print(f'[fsguard] skip  {f["dir"]}/{f["name"]} (never restored)')
            continue
        blob = (src / f['local']).read_bytes()
        if sha256(blob) != f['sha256']:
            print(f'[fsguard] FAIL  {f["dir"]}/{f["name"]}: local backup corrupt')
            failed += 1
            continue
        blobs[(f['dir'], f['name'])] = blob
    for (d, name) in [k for k in blobs if k[1] == '.wip']:
        chunk = blobs.pop((d, name))
        try:
            hdr = wip_info(chunk)
        except MergeError as exc:
            print(f'[fsguard] skip  {d}/.wip ({exc})')
            continue
        day = time.strftime('%Y%m%d', time.localtime(hdr.t0)) + '.h5'
        pending[(d, day)] = chunk
    return blobs, pending, failed


def _absorb_wip(rig: Rig, d: str, bak_blob: bytes | None, dev_blob: bytes | None,
                chunk: bytes, dev_wip_cache: dict) -> bytes | None:
    """Fold the backed-up .wip into the backup's day blob — unless the device
    still owns that block.

    Two situations mean the records will arrive (or already arrived) on their
    own and absorbing would DUPLICATE them:
      - the device's live .wip has the same t0: the block is still open there
        and will be sealed by the device with >= our records;
      - the device's day file already has a sealed block with that t0 and
        >= count: the seal already happened.
    Both only occur when restore runs WITHOUT a wipe in between (aborted
    battery, dry-run rehearsals). After a real OTA wipe the device's .wip
    starts at a fresh t0 and the backup's block is absorbed.
    """
    hdr = wip_info(chunk)
    if d not in dev_wip_cache:
        dev_wip_cache[d] = rig.download(f'{d.rstrip("/")}/.wip')
    dev_wip = dev_wip_cache[d]
    if dev_wip is not None:
        try:
            if wip_info(dev_wip).t0 == hdr.t0:
                print(f'[fsguard] wip   skipped: block t0={hdr.t0} still open '
                      f'on the device')
                return bak_blob
        except MergeError:
            pass                      # unreadable live .wip decides nothing
    if dev_blob is not None:
        for e in h5.scan(dev_blob):
            if e.kind == 'data' and e.header.t0 == hdr.t0 \
                    and e.header.count >= hdr.count:
                print(f'[fsguard] wip   skipped: t0={hdr.t0} already sealed '
                      f'on the device ({e.header.count} >= {hdr.count})')
                return bak_blob
    schema_src = bak_blob if bak_blob is not None else dev_blob
    if schema_src is None:
        print(f'[fsguard] wip   skipped: no schema source for t0={hdr.t0}')
        return bak_blob
    pseudo = _schema_chunk_of(schema_src) + chunk
    if bak_blob is None:
        print(f'[fsguard] wip   {hdr.count} unsealed record(s) form the '
              f'backup side (day file was not in backup)')
        return pseudo
    try:
        merged, _ = merge_blobs([bak_blob, pseudo], labels=['backup', '.wip'])
    except MergeError as exc:
        print(f'[fsguard] wip   NOT absorbed ({exc}) — restoring day file as-is')
        return bak_blob
    print(f'[fsguard] wip   {hdr.count} unsealed record(s) absorbed into the '
          f'backup side')
    return merged


def cmd_restore(rig: Rig, src: Path, dry: bool) -> int:
    manifest = json.loads((src / 'manifest.json').read_text())
    blobs, pending, failed = _load_backup(src, manifest)
    dev_wip_cache: dict = {}
    for key in sorted(set(blobs) | set(pending)):
        d, name = key
        remote = f'{d.rstrip("/")}/{name}'
        bak_blob = blobs.get(key)
        try:
            dev_blob = rig.download(remote)
            if key in pending:
                bak_blob = _absorb_wip(rig, d, bak_blob, dev_blob,
                                       pending[key], dev_wip_cache)
                if bak_blob is None:
                    continue
            if dev_blob == bak_blob:
                print(f'[fsguard] ok    {remote} (already identical)')
                continue
            if dry:
                act = 'upload' if dev_blob is None else \
                    ('merge' if name.endswith('.h5') else 'overwrite')
                print(f'[fsguard] would {act} {remote}')
                continue
            if dev_blob is not None and name.endswith('.h5'):
                note = _restore_merge(rig, remote, d, name, bak_blob, dev_blob)
                print(f'[fsguard] merge {remote}: {note}')
                continue
            rig.upload(d, name, bak_blob)
            back = rig.download(remote)
            if back != bak_blob:
                raise RuntimeError('verify readback differs')
            print(f'[fsguard] up    {remote} ({len(bak_blob)} B, verified)')
        except (RuntimeError, MergeError, requests.RequestException) as exc:
            print(f'[fsguard] FAIL  {remote}: {exc}')
            failed += 1
    print(f'[fsguard] restore: {failed} failure(s)')
    return 1 if failed else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('cmd', choices=('backup', 'restore'))
    ap.add_argument('--host', required=True)
    ap.add_argument('--out', help='backup: destination directory')
    ap.add_argument('--from', dest='src', help='restore: backup directory')
    ap.add_argument('--dirs', nargs='+', default=['/history'],
                    help='backup: device directories to save (default: /history)')
    ap.add_argument('--user', default=os.environ.get('SIMUT_WEB_USER'))
    ap.add_argument('--password', default=os.environ.get('SIMUT_WEB_PASS'))
    ap.add_argument('--dry-run', action='store_true')
    args = ap.parse_args()

    if not args.user or not args.password:
        print('credentials missing: set SIMUT_WEB_USER / SIMUT_WEB_PASS '
              'or pass --user/--password', file=sys.stderr)
        return 2
    rig = Rig(args.host, args.user, args.password)
    if args.cmd == 'backup':
        if not args.out:
            print('backup needs --out', file=sys.stderr)
            return 2
        return cmd_backup(rig, Path(args.out), args.dirs)
    if not args.src:
        print('restore needs --from', file=sys.stderr)
        return 2
    return cmd_restore(rig, Path(args.src), args.dry_run)


if __name__ == '__main__':
    sys.exit(main())
