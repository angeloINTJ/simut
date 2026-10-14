#!/usr/bin/env python3
"""The manifest a release publishes, with the Pico 2 W's images in it.

tools/release_manifest.py runs in the publish job of .github/workflows/
release-ota.yml, over the signed images. Since step 7 of
docs/analysis/OTA_AB_RP2350.md it publishes a fourth image, the Pico 2 W's
(env `releasetwo`), and its two files are not one image in two wrappings, as
the Pico W's are:

  * its .bin is the update, flagged for trial (tools/rp2350/picobin.py) and
    signed. The manifest refuses one without the flag: it would install without
    a trial, so a bad one could not come back;
  * its .uf2 is the factory image: the partition table, as the build's picotool
    wrote it, then that signed image in slot A with the flag cleared, every
    block in the absolute family, numbered as one download. Those are the bytes
    the boot ROM leaves in flash when an update buys itself, so a board flashed
    over USB holds what a board updated over the air holds;
  * the Pico W's three entries come out as before: same files, same .uf2
    writer;
  * the workflow's build, sign and verify loops name the images the manifest
    expects, and no profile behind them trusts the bench root. The signing job
    would refuse such an image at a tag; this says so on every push.

The factory writer (tools/rp2350/uf2.py) is held to picotool's own output by
tools/check_rp2350_image.py after every pico2_w_release link, where picotool is
at hand. Here it runs on synthetic images, and the expected file is built
without it.

Run: python3 tools/test_release_manifest.py
Exit status is 0 on pass, 1 on failure.
"""

import hashlib
import json
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import gen_features as gf  # noqa: E402
import release_manifest as rm  # noqa: E402

WORKFLOW = REPO / ".github" / "workflows" / "release-ota.yml"
TOOL = REPO / "tools" / "release_manifest.py"
ABSOLUTE, RP2040 = 0xE48BFF57, 0xE48BFF56
SLOT_A = 0x10002000
EXE_RP2350, TBYB = 0x1021, 0x8000
TRAILER = bytes((i * 7) & 0xFF for i in range(241))   # where the signature goes; nothing reads it here
fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
    return cond


def version():
    return rm.version_from_source()


def pico_w(env, n=3000):
    """A Pico W image: bytes, and the tag the device and the manifest read."""
    img = bytearray((i * 13 + len(env)) & 0xFF for i in range(n))
    tag = f"SIMUT-ENV:{env};v={version()};".encode()
    img[700:700 + len(tag)] = tag
    return bytes(img)


def pico2(flagged=True, env="releasetwo", n=5000):
    """A Pico 2 W image as pico2_w_release links it: its image definition at
    0x124, a loop of one block, and its tag; signed (a trailer after it)."""
    img = bytearray((i * 31 + 7) & 0xFF for i in range(n))
    flags = EXE_RP2350 | (TBYB if flagged else 0)
    words = [0xFFFFDED3, 0x42 | (1 << 8) | (flags << 16), 0xFF | (1 << 8), 0, 0xAB123579]
    img[0x124:0x124 + 20] = struct.pack("<5I", *words)
    tag = f"SIMUT-ENV:{env};v={version()};".encode()
    img[2000:2000 + len(tag)] = tag
    return bytes(img) + TRAILER


def uf2_block(addr, payload, family, no=0, n=1, flags=0x2000):
    b = bytearray(512)
    struct.pack_into("<8I", b, 0, 0x0A324655, 0x9E5D5157, flags, addr, 256, no, n, family)
    b[32:32 + len(payload)] = payload
    struct.pack_into("<I", b, 508, 0x0AB16F30)
    return bytes(b)


TABLE = uf2_block(0x10000000, bytes((i * 5 + 1) & 0xFF for i in range(256)), ABSOLUTE)


def layout(root, images, table=TABLE):
    """A build directory as the release's sign job leaves it."""
    for pio_env, data in images.items():
        d = root / "build" / pio_env
        d.mkdir(parents=True)
        (d / "firmware.bin").write_bytes(data)
        if pio_env == "pico2_w_release" and table is not None:
            (d / "partition_table.uf2").write_bytes(table)


def all_images(**over):
    imgs = {"pico_w_release": pico_w("release"), "pico_w_alpha": pico_w("alpha"),
            "pico_w_air": pico_w("air"), "pico2_w_release": pico2()}
    imgs.update(over)
    return imgs


def run(root):
    r = subprocess.run([sys.executable, str(TOOL), "--out", str(root / "out"),
                        "--build-dir", str(root / "build")], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def blocks(data):
    return [struct.unpack_from("<8I", data, i) + (data[i + 32:i + 508],)
            for i in range(0, len(data), 512)]


def test_pico_w_writer_unchanged():
    # main's bin_to_uf2 on this input, 2026-10-04, before the Pico 2 W joined.
    data = bytes((i * 31 + 7) & 0xFF for i in range(1000))
    got = hashlib.sha256(rm.bin_to_uf2(data)).hexdigest()
    check(got == "b628311351110a3dc737ea92183a676cbee0e2291fe7418ce15291fe9e35bae1",
          f"bin_to_uf2 writes the Pico W's .uf2 differently now ({got})")


def test_four_entries():
    with tempfile.TemporaryDirectory() as t:
        root = Path(t)
        imgs = all_images()
        layout(root, imgs)
        code, out = run(root)
        if not check(code == 0, f"the manifest failed on four good images: {out}"):
            return
        man = json.loads((root / "out" / "manifest.json").read_text(encoding="utf-8"))
        check(list(man["images"]) == ["release", "alpha", "air", "releasetwo"],
              f"the manifest lists {list(man['images'])}")
        check(man["version"] == version(), "the manifest's version is not the source's")
        envs = {"release": "pico_w_release", "alpha": "pico_w_alpha", "air": "pico_w_air",
                "releasetwo": "pico2_w_release"}
        for env, entry in man["images"].items():
            for key, f in (("bin", entry), ("uf2", entry.get("uf2", {}))):
                path = root / "out" / f.get("file", "?")
                if not check(path.is_file(), f"{env}: the {key} file {f.get('file')} was not written"):
                    continue
                data = path.read_bytes()
                check(f["size"] == len(data) and f["sha256"] == hashlib.sha256(data).hexdigest(),
                      f"{env}: the {key} entry does not describe its file")
            check(entry["file"] == f"simut_v{version()}_{env}.bin", f"{env}: named {entry['file']}")
            check((root / "out" / entry["file"]).read_bytes() == imgs[envs[env]],
                  f"{env}: the published .bin is not the signed image")
        for env in ("release", "alpha", "air"):
            uf2 = (root / "out" / man["images"][env]["uf2"]["file"]).read_bytes()
            check(uf2 == rm.bin_to_uf2(imgs[envs[env]]), f"{env}: the .uf2 is not the Pico W's wrapping")
            check({b[7] for b in blocks(uf2)} == {RP2040}, f"{env}: a .uf2 block outside the RP2040 family")


def test_pico2_factory_image():
    with tempfile.TemporaryDirectory() as t:
        root = Path(t)
        layout(root, all_images())
        code, out = run(root)
        if not check(code == 0, f"the manifest failed: {out}"):
            return
        man = json.loads((root / "out" / "manifest.json").read_text(encoding="utf-8"))
        uf2 = (root / "out" / man["images"]["releasetwo"]["uf2"]["file"]).read_bytes()
        want_img = pico2(flagged=False)          # the signed update, flag cleared, trailer kept
        pages = (len(want_img) + 255) // 256
        bl = blocks(uf2)
        n = len(bl)
        check(n == 1 + pages, f"the factory .uf2 has {n} blocks, want the table and {pages} pages")
        check(len(uf2) % 512 == 0 and all(b[0] == 0x0A324655 and b[1] == 0x9E5D5157 for b in bl),
              "the factory .uf2 is not whole UF2 blocks")
        check([(b[5], b[6]) for b in bl] == [(i, n) for i in range(n)],
              "the factory .uf2 is not numbered 0..n-1 of n, as one download")
        check(uf2[:20] == TABLE[:20] and uf2[28:512] == TABLE[28:512],
              "the factory .uf2 does not start with the table's block as picotool wrote it")
        prog = bl[1:]
        check([b[3] for b in prog] == [SLOT_A + 256 * i for i in range(pages)],
              "the program is not at slot A, page after page")
        check({(b[2], b[4], b[7]) for b in prog} == {(0x2000, 256, ABSOLUTE)},
              "a program block is not a 256-byte page in the absolute family")
        payload = b"".join(b[8][:256] for b in prog)
        check(payload[:len(want_img)] == want_img and not any(payload[len(want_img):]),
              "the factory .uf2 does not hold the signed update with its trial flag cleared")
        check(not any(any(b[8][256:]) for b in prog), "the bytes after a page are not zero")
        sent = (root / "out" / man["images"]["releasetwo"]["file"]).read_bytes()
        diff = [i for i in range(len(sent)) if sent[i] != payload[i]]
        check(diff == [0x12B], f"the .uf2 and the .bin differ at {[hex(d) for d in diff[:6]]}, "
                               "want the trial flag's byte alone")


def refused(images, why, table=TABLE):
    with tempfile.TemporaryDirectory() as t:
        root = Path(t)
        layout(root, images, table)
        code, out = run(root)
        check(code == 1 and why in out, f"want exit 1 with '{why}', got {code}: {out.strip()[-300:]}")
        check(not (root / "out" / "manifest.json").exists(), f"a manifest was written although {why}")


def test_pico2_refusals():
    refused(all_images(pico2_w_release=pico2(flagged=False)), "not flagged for trial")
    refused(all_images(), "partition_table.uf2", table=None)
    refused(all_images(), "not the partition table",
            table=uf2_block(0x10000000, bytes(256), 0xE48BFF59))
    refused(all_images(), "not the partition table",
            table=uf2_block(0x10002000, bytes(256), ABSOLUTE))
    refused(all_images(), "not a whole number of UF2 blocks", table=TABLE[:300])
    refused(all_images(pico2_w_release=pico2(env="release")), "tag says release")


def test_the_workflow_publishes_these():
    text = WORKFLOW.read_text(encoding="utf-8")
    want = set(rm.ENVS.values())
    loops = re.findall(r"for (\w+) in ([\w: ]+); do", text)
    check(len(loops) == 3, f"release-ota.yml has {len(loops)} image loops, want build, sign and verify")
    for var, names in loops:
        got = [n.split(":")[0] for n in names.split()]
        check(set(got) == want and len(got) == len(want),
              f"the loop over {var} names {got}, the manifest expects {sorted(want)}")
        pairs = [n.split(":") for n in names.split() if ":" in n]
        check(all(rm.ENVS.get(env) == pio for pio, env in pairs),
              f"the loop over {var} pairs {pairs}, the manifest has {rm.ENVS}")
    installed = set(re.findall(r"pio pkg install -e (\w+)", text))
    check(installed == want, f"the build installs the packages of {sorted(installed)}, want {sorted(want)}")
    M = gf.load_manifest()
    for pio_env in sorted(want):
        prof = gf.resolve_profile(pio_env, M["profiles"])
        check(not prof.get("ota_trust_bench"),
              f"{pio_env} trusts the bench root, and the release publishes its image")


def main():
    for t in (test_pico_w_writer_unchanged, test_four_entries, test_pico2_factory_image,
              test_pico2_refusals, test_the_workflow_publishes_these):
        before = len(fails)
        try:
            t()
        except Exception as e:  # a crash is a failure with its reason
            fails.append(f"{t.__name__}: {type(e).__name__}: {e}")
        print(f"  {'ok  ' if len(fails) == before else 'FAIL'} {t.__name__}")
    for f in fails:
        print(f"  FAIL {f}")
    print("PASS" if not fails else f"FAIL ({len(fails)})")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
