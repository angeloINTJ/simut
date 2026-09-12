#!/usr/bin/env python3
"""release_manifest.py — the manifest a fleet manager downloads before an OTA.

Reads the .bin of every hardware variant PlatformIO built, checks that each
one carries the SIMUT-ENV tag for the variant it claims to be, and writes
`manifest.json` next to the renamed images:

    {
      "version": "2.4.2-beta",
      "min_from": "1.6.2",
      "images": {
        "release": {"file": "simut_v2.4.2-beta_release.bin", "size": N, "sha256": "…"},
        "alpha":   {...},
        "air":     {...}
      }
    }

Why it exists: the firmware cannot pick an image for the operator — the app
has to, and the only thing that stops it from staging an alpha image into an
Air is knowing, before the upload, which file is which. The tag inside the
.bin is the device-side half of that (ota_validate_staging); this manifest is
the client-side half. `min_from` is the oldest firmware whose OTA path can
apply this release (docs/OTA_USAGE.md).

Usage:
    python3 tools/release_manifest.py [--out release/ota] [--min-from 1.6.2]

Exit 1 if an image is missing its tag or tagged for another variant: a
manifest that lies is worse than no manifest.
"""
import argparse, hashlib, json, os, re, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENVS = {"release": "pico_w_release", "alpha": "pico_w_alpha", "air": "pico_w_air"}
TAG = re.compile(rb"SIMUT-ENV:([a-z]+);v=([^;]+);")


def version_from_source():
    src = open(os.path.join(ROOT, "src", "SystemDefs_Limits.h"), encoding="utf-8").read()
    m = re.search(r'#define SIMUT_VERSION "([^"]+)"', src)
    if not m:
        sys.exit("SIMUT_VERSION not found in src/SystemDefs_Limits.h")
    return m.group(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "release", "ota"))
    ap.add_argument("--min-from", default="1.6.2")
    ap.add_argument("--build-dir", default=os.path.join(ROOT, ".pio", "build"))
    args = ap.parse_args()

    version = version_from_source()
    os.makedirs(args.out, exist_ok=True)
    images = {}
    bad = 0
    for env, pio_env in ENVS.items():
        src = os.path.join(args.build_dir, pio_env, "firmware.bin")
        if not os.path.exists(src):
            print(f"[manifest] missing {src} — build {pio_env} first", file=sys.stderr)
            bad += 1
            continue
        data = open(src, "rb").read()
        m = TAG.search(data)
        if not m:
            print(f"[manifest] {pio_env}: no SIMUT-ENV tag in the image", file=sys.stderr)
            bad += 1
            continue
        tag_env, tag_ver = m.group(1).decode(), m.group(2).decode()
        if tag_env != env or tag_ver != version:
            print(f"[manifest] {pio_env}: tag says {tag_env} v{tag_ver}, expected {env} v{version}", file=sys.stderr)
            bad += 1
            continue
        name = f"simut_v{version}_{env}.bin"
        shutil.copyfile(src, os.path.join(args.out, name))
        images[env] = {"file": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        print(f"[manifest] {env}: {name} {len(data)} B")

    if bad:
        sys.exit(1)
    manifest = {"version": version, "min_from": args.min_from, "images": images}
    with open(os.path.join(args.out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"[manifest] wrote {os.path.join(args.out, 'manifest.json')}")


if __name__ == "__main__":
    main()
