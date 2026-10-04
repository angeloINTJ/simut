#!/usr/bin/env python3
"""release_manifest.py — the manifest a fleet manager downloads before an OTA.

Reads the .bin of every hardware variant PlatformIO built, checks that each
one carries the SIMUT-ENV tag for the variant it claims to be, and writes
`manifest.json` next to the renamed images:

    {
      "version": "2.4.2-beta",
      "min_from": "1.6.2",
      "images": {
        "release": {"file": "simut_v2.4.2-beta_release.bin", "size": N, "sha256": "…",
                    "uf2": {"file": "simut_v2.4.2-beta_release.uf2", "size": M, "sha256": "…"}},
        "alpha":   {...},
        "air":     {...},
        "releasetwo": {...}
      }
    }

`releasetwo` is the Pico 2 W (RP2350), published since step 7 of
docs/analysis/OTA_AB_RP2350.md, and its two files are not one image in two
wrappings. Its .bin is the update, flagged for trial (tools/rp2350/picobin.py);
one without the flag is refused, because it would install without a trial and a
bad one could not come back. Its .uf2 is the factory image (tools/rp2350/uf2.py):
the partition table the build wrote, then the signed update in slot A with the
flag cleared. Those are the bytes the boot ROM leaves in flash when an update
buys itself, so a board flashed over USB holds what a board updated over the air
holds.

Why it exists: the firmware cannot pick an image for the operator — the app
has to, and the only thing that stops it from staging an alpha image into an
Air is knowing, before the upload, which file is which. The tag inside the
.bin is the device-side half of that (ota_validate_staging); this manifest is
the client-side half. `min_from` is the oldest firmware whose OTA path can
apply this release (docs/OTA_USAGE.md).

Usage:
    python3 tools/release_manifest.py [--out release/ota] [--min-from 1.6.2]

Exit 1 if an image is missing its tag or tagged for another variant, or if the
Pico 2 W's update is not flagged for trial or has no partition table beside it:
a manifest that lies is worse than no manifest.
"""
import argparse, hashlib, json, os, re, shutil, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "rp2350"))
import gen_memmap  # noqa: E402  (tools/rp2350/gen_memmap.py)
import picobin  # noqa: E402  (tools/rp2350/picobin.py)
import uf2 as rp2350_uf2  # noqa: E402  (tools/rp2350/uf2.py)

ENVS = {"release": "pico_w_release", "alpha": "pico_w_alpha", "air": "pico_w_air",
        "releasetwo": "pico2_w_release"}
TAG = re.compile(rb"SIMUT-ENV:([a-z]+);v=([^;]+);")
# The RP2350's env carries the chip's suffix (SIMUT_ENV_CHIP, src/simut_config.h).
RP2350_SUFFIX = "two"

# UF2 do RP2040 (R16): embrulha o .bin de flash inteira (base 0x10000000) no
# .uf2 que a F10 grava num Pico virgem pelo BOOTSEL. Mesma saida do picotool e
# do firmware.uf2 do arduino-pico — provado byte a byte contra o .uf2 que o
# build antigo publicava —, entao o .uf2 sempre casa com o .bin do manifesto.
UF2_M0, UF2_M1, UF2_MEND = 0x0A324655, 0x9E5D5157, 0x0AB16F30
UF2_FAMILY_RP2040, UF2_FLAG_FAMILY = 0xE48BFF56, 0x2000
UF2_BASE, UF2_PAYLOAD = 0x10000000, 256


def bin_to_uf2(data):
    n = (len(data) + UF2_PAYLOAD - 1) // UF2_PAYLOAD
    out = bytearray()
    for i in range(n):
        chunk = data[i * UF2_PAYLOAD:(i + 1) * UF2_PAYLOAD]
        payload = chunk + b"\x00" * (476 - len(chunk))
        out += struct.pack("<IIIIIIII", UF2_M0, UF2_M1, UF2_FLAG_FAMILY,
                           UF2_BASE + i * UF2_PAYLOAD, UF2_PAYLOAD, i, n,
                           UF2_FAMILY_RP2040) + payload + struct.pack("<I", UF2_MEND)
    return bytes(out)


def rp2350_factory(data, table_path):
    """The Pico 2 W's .uf2 from its signed update; ValueError says what is wrong."""
    try:
        image = picobin.clear_trial(data)
    except ValueError as e:
        raise ValueError(f"{e}; the update a release publishes is firmware_ota.bin, signed") from None
    if not os.path.exists(table_path):
        raise ValueError(f"no {table_path}: the factory .uf2 needs the build's partition table")
    table = open(table_path, "rb").read()
    slot_a = gen_memmap.XIP + gen_memmap.partitions()["A"][0]
    return rp2350_uf2.factory(table, image, slot_a)


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
        if env.endswith(RP2350_SUFFIX):
            try:
                uf2 = rp2350_factory(data, os.path.join(os.path.dirname(src), "partition_table.uf2"))
            except ValueError as e:
                print(f"[manifest] {pio_env}: {e}", file=sys.stderr)
                bad += 1
                continue
        else:
            # .uf2 da mesma imagem (R16): a F10 grava um Pico virgem com ele.
            uf2 = bin_to_uf2(data)
        name = f"simut_v{version}_{env}.bin"
        shutil.copyfile(src, os.path.join(args.out, name))
        images[env] = {"file": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        print(f"[manifest] {env}: {name} {len(data)} B")
        uf2name = f"simut_v{version}_{env}.uf2"
        with open(os.path.join(args.out, uf2name), "wb") as f:
            f.write(uf2)
        images[env]["uf2"] = {"file": uf2name, "size": len(uf2), "sha256": hashlib.sha256(uf2).hexdigest()}
        print(f"[manifest] {env}: {uf2name} {len(uf2)} B")

    if bad:
        sys.exit(1)
    manifest = {"version": version, "min_from": args.min_from, "images": images}
    with open(os.path.join(args.out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"[manifest] wrote {os.path.join(args.out, 'manifest.json')}")


if __name__ == "__main__":
    main()
