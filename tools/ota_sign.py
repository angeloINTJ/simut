#!/usr/bin/env python3
"""
ota_sign.py — keys, certificates and signatures for OTA images that only take signed firmware.

Design: docs/analysis/OTA_ASSINADA.md. Two tiers of ECDSA P-256 keys:

  root     offline, kept by the maintainer, its public key compiled into every image;
           it signs nothing but signer certificates.
  signer   certified by a root (scope, serial); it signs the images. The release signer lives
           in a GitHub Environment that waits for the maintainer's approval.

A signed image is the unchanged .bin followed by a 241-byte trailer:

  off  len  field
    0    4  security_version         u32 LE  (anti-rollback level)
    4   16  env                      ASCII, NUL-padded, no "SIMUT-ENV:" prefix
   20    4  image_len                u32 LE  (bytes of .bin the signature covers)
   24  137  certificate:
             24    4  serial         u32 LE
             28    1  scope          1 = release, 2 = bench
             29    3  reserved       zero
             32   65  signer public key, uncompressed P-256 point (0x04 || X || Y)
             97   64  root signature, raw r || s, over SHA-256(CERT_DOMAIN || bytes 24..97)
  161   64  image signature, raw r || s, over SHA-256(IMAGE_DOMAIN || image || bytes 0..161)
  225   16  footer: trailer_len u32 LE (241), format u16 LE (1), reserved u16, b"SIMUTSIG"

The device (src/ota/signature.cpp) runs the same checks as `verify` below, in the same order.

Subcommands
  root-new     a root key, encrypted with a passphrase typed here; writes the public key file
  signer-new   a signer key and its certificate, signed by a root
  sign         append the trailer to a .bin
  verify       check a signed .bin the way the device does
  vectors      the host-test vectors (test/test_ota_sig/vectors.h), from a fixed test key
  selftest     round trips and tamper cases, with real ECDSA

The private keys never leave the files given; nothing here prints one.

Project: SIMUT. License: MIT.
"""

import argparse
import getpass
import hashlib
import os
import struct
import sys

try:
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import (
        Prehashed, decode_dss_signature, encode_dss_signature)
    from cryptography.exceptions import InvalidSignature
except ImportError:  # pragma: no cover - the message is the point
    sys.exit("ota_sign.py needs the 'cryptography' package: pip install cryptography")

TRAILER_LEN = 241
FORMAT_V1 = 1
MAGIC = b"SIMUTSIG"
CERT_BODY_LEN = 73          # serial(4) scope(1) reserved(3) pubkey(65)
CERT_LEN = CERT_BODY_LEN + 64
SIGNED_PREFIX_LEN = 161     # everything before the image signature
CERT_DOMAIN = b"SIMUT-OTA-CERT-v1"
IMAGE_DOMAIN = b"SIMUT-OTA-IMG-v1"
SCOPES = {"release": 1, "bench": 2}
ENV_LEN = 16

# Device refusals (v= on the stage reply). 8..12 are new; a signed env that is not the
# running variant is the same refusal validation.cpp already gives a wrong tag, 7.
V_OK, V_ENV, V_MISSING, V_INVALID, V_REVOKED, V_ROLLBACK, V_SCOPE = 0, 7, 8, 9, 10, 11, 12


# ── primitives ──────────────────────────────────────────────────────────────

def pub_bytes(key):
    """Uncompressed P-256 point of a private or public key."""
    pub = key.public_key() if hasattr(key, "public_key") else key
    return pub.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)


def pub_from_bytes(raw):
    return ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), bytes(raw))


def sign_digest(key, digest):
    der = key.sign(digest, ec.ECDSA(Prehashed(hashes.SHA256())))
    r, s = decode_dss_signature(der)
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def verify_digest(pub_raw, digest, sig_raw):
    try:
        pub = pub_from_bytes(pub_raw)
    except ValueError:
        return False
    r = int.from_bytes(sig_raw[:32], "big")
    s = int.from_bytes(sig_raw[32:], "big")
    try:
        pub.verify(encode_dss_signature(r, s), digest, ec.ECDSA(Prehashed(hashes.SHA256())))
        return True
    except InvalidSignature:
        return False


def cert_digest(cert_body):
    return hashlib.sha256(CERT_DOMAIN + cert_body).digest()


def image_digest(image, signed_prefix):
    return hashlib.sha256(IMAGE_DOMAIN + image + signed_prefix).digest()


def make_cert(root_key, signer_pub_raw, serial, scope):
    body = struct.pack("<IB3x", serial, scope) + signer_pub_raw
    assert len(body) == CERT_BODY_LEN
    return body + sign_digest(root_key, cert_digest(body))


def env_field(env):
    raw = env.encode("ascii")
    if not raw or len(raw) >= ENV_LEN or b"SIMUT-ENV:" in raw:
        raise ValueError(f"env must be 1..{ENV_LEN - 1} ASCII bytes, without the tag prefix: {env!r}")
    return raw.ljust(ENV_LEN, b"\0")


def build_trailer(image, signer_key, cert, security_version, env, raw_env=None):
    """raw_env: the 16 env bytes as given, unchecked — only for test vectors of a
    malformed but genuinely signed trailer."""
    if len(cert) != CERT_LEN:
        raise ValueError("a certificate is 137 bytes")
    envb = raw_env if raw_env is not None else env_field(env)
    prefix = struct.pack("<I", security_version) + envb + struct.pack("<I", len(image)) + cert
    assert len(prefix) == SIGNED_PREFIX_LEN
    sig = sign_digest(signer_key, image_digest(image, prefix))
    footer = struct.pack("<IHH", TRAILER_LEN, FORMAT_V1, 0) + MAGIC
    trailer = prefix + sig + footer
    assert len(trailer) == TRAILER_LEN
    return trailer


def check(blob, anchors, min_serial, min_security, running_env):
    """The device's decision, in the device's order.

    anchors: list of (root_pub_raw, scope) this image trusts.
    min_serial: {scope: lowest serial still accepted}.
    Returns (v, info) with v one of the V_* codes."""
    info = {}
    if len(blob) < TRAILER_LEN + 1 or blob[-8:] != MAGIC:
        return V_MISSING, info
    tlen, fmt, _ = struct.unpack("<IHH", blob[-16:-8])
    if tlen != TRAILER_LEN or fmt != FORMAT_V1:
        return V_INVALID, info
    t = blob[-TRAILER_LEN:]
    image = blob[:-TRAILER_LEN]
    secver, = struct.unpack("<I", t[0:4])
    env_raw = t[4:20]
    image_len, = struct.unpack("<I", t[20:24])
    cert = t[24:161]
    serial, scope = struct.unpack("<IB", cert[0:5])
    signer_pub = cert[8:73]
    root_sig = cert[73:137]
    info.update(security_version=secver, serial=serial, scope=scope, image_len=image_len)
    if image_len != len(image) or b"\0" not in env_raw or cert[5:8] != b"\0\0\0":
        return V_INVALID, info
    env = env_raw.split(b"\0", 1)[0].decode("ascii", "replace")
    info["env"] = env
    roots = [pub for pub, sc in anchors if sc == scope]
    if not roots:
        return V_SCOPE, info
    if not any(verify_digest(pub, cert_digest(cert[:CERT_BODY_LEN]), root_sig) for pub in roots):
        return V_INVALID, info
    if serial < min_serial.get(scope, 0):
        return V_REVOKED, info
    if not verify_digest(signer_pub, image_digest(image, t[:SIGNED_PREFIX_LEN]), t[161:225]):
        return V_INVALID, info
    if secver < min_security:
        return V_ROLLBACK, info
    if env != running_env:
        return V_ENV, info
    return V_OK, info


# ── key files ───────────────────────────────────────────────────────────────

def read_passphrase(prompt, confirm=False):
    p = getpass.getpass(prompt)
    if confirm and getpass.getpass("Again: ") != p:
        sys.exit("the two passphrases differ; nothing written")
    if confirm and len(p) < 12:
        sys.exit("use a passphrase of 12 characters or more; nothing written")
    return p.encode()


def load_key(path, prompt="Passphrase for %s: "):
    data = open(path, "rb").read()
    try:
        return serialization.load_pem_private_key(data, password=None)
    except TypeError:
        return serialization.load_pem_private_key(data, password=read_passphrase(prompt % path))


def write_private(path, key, passphrase):
    enc = (serialization.BestAvailableEncryption(passphrase) if passphrase
           else serialization.NoEncryption())
    pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, enc)
    if os.path.exists(path):
        sys.exit(f"{path} exists; refusing to overwrite a key")
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(pem)


def write_pub(path, raw):
    with open(path, "w") as f:
        f.write(raw.hex() + "\n")


def read_pub(path):
    raw = bytes.fromhex(open(path).read().strip())
    pub_from_bytes(raw)  # refuses a point not on the curve
    return raw


# ── subcommands ─────────────────────────────────────────────────────────────

def cmd_root_new(a):
    key = ec.generate_private_key(ec.SECP256R1())
    write_private(a.out, key, read_passphrase("Passphrase for the new root: ", confirm=True))
    write_pub(a.pub_out, pub_bytes(key))
    print(f"root written to {a.out} (encrypted); public key {a.pub_out}: {pub_bytes(key).hex()}")


def cmd_signer_new(a):
    root = load_key(a.root)
    key = ec.generate_private_key(ec.SECP256R1())
    cert = make_cert(root, pub_bytes(key), a.serial, SCOPES[a.scope])
    passphrase = None if a.no_encrypt else read_passphrase("Passphrase for the new signer: ", confirm=True)
    write_private(a.key_out, key, passphrase)
    with open(a.cert_out, "wb") as f:
        f.write(cert)
    print(f"signer {a.scope} serial {a.serial}: key {a.key_out}, certificate {a.cert_out}")


def cmd_sign(a):
    image = open(a.input, "rb").read()
    if image[-8:] == MAGIC:
        sys.exit(f"{a.input} is already signed")
    key = load_key(a.key)
    cert = open(a.cert, "rb").read()
    trailer = build_trailer(image, key, cert, a.security_version, a.env)
    blob = image + trailer
    if a.root_pub:
        anchors = [(read_pub(p), cert[4]) for p in a.root_pub]
        v, _ = check(blob, anchors, {}, 0, a.env)
        if v != V_OK:
            sys.exit(f"the signed image does not verify (v={v}); nothing written")
    with open(a.output, "wb") as f:
        f.write(blob)
    print(f"{a.output}: {len(image)} + {TRAILER_LEN} B, security_version {a.security_version}, env {a.env}")


def cmd_verify(a):
    blob = open(a.input, "rb").read()
    anchors = [(read_pub(p), SCOPES["release"]) for p in (a.root_pub or [])]
    anchors += [(read_pub(p), SCOPES["bench"]) for p in (a.bench_root_pub or [])]
    min_serial = {SCOPES["release"]: a.min_serial, SCOPES["bench"]: a.min_bench_serial}
    v, info = check(blob, anchors, min_serial, a.min_security, a.env)
    print(f"v={v} {info}")
    return 0 if v == V_OK else 1


# ── test vectors ────────────────────────────────────────────────────────────
# A fixed TEST key set, published on purpose: no image trusts it. The scalars are
# arbitrary constants, so the vectors can be regenerated and reviewed.
TEST_ROOT = 0x5131_4d55_5420_5445_5354_2052_4f4f_5420_4b45_5920_4e4f_5420_5452_5553_5445_4431
TEST_BENCH_ROOT = 0x5131_4d55_5420_5445_5354_2042_454e_4348_2052_4f4f_5420_4e4f_5420_5452_5553_5432
TEST_SIGNER = 0x5131_4d55_5420_5445_5354_2053_4947_4e45_5220_4e4f_5420_5452_5553_5445_4420_2033
TEST_BENCH_SIGNER = 0x5131_4d55_5420_5445_5354_2042_454e_4348_2053_4947_4e45_5220_4e4f_5420_2034
TEST_ROGUE_ROOT = 0x5131_4d55_5420_5445_5354_2052_4f47_5545_2052_4f4f_5420_4e4f_5420_5452_2035


def test_key(scalar):
    return ec.derive_private_key(scalar, ec.SECP256R1())


def test_image(n=1029):  # not a multiple of the device's 512-byte chunk: the last one is partial
    out, x = bytearray(), 0x12345678
    for _ in range(n):  # xorshift32: the same bytes on every run
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out.append(x & 0xFF)
    return bytes(out)


def build_vectors():
    root, bench_root, rogue = test_key(TEST_ROOT), test_key(TEST_BENCH_ROOT), test_key(TEST_ROGUE_ROOT)
    signer, bench_signer = test_key(TEST_SIGNER), test_key(TEST_BENCH_SIGNER)
    image = test_image()
    cert = make_cert(root, pub_bytes(signer), 5, SCOPES["release"])
    bench_cert = make_cert(bench_root, pub_bytes(bench_signer), 2, SCOPES["bench"])
    rogue_cert = make_cert(rogue, pub_bytes(signer), 5, SCOPES["release"])
    # A release-scope certificate signed by the BENCH root: a bench image trusts that
    # root, but only for bench signers, so this must not pass as a release signer.
    forged_cert = make_cert(bench_root, pub_bytes(bench_signer), 9, SCOPES["release"])
    good = image + build_trailer(image, signer, cert, 3, "pico_w_test")
    bench = image + build_trailer(image, bench_signer, bench_cert, 3, "pico_w_test")
    rogue_blob = image + build_trailer(image, signer, rogue_cert, 3, "pico_w_test")
    forged = image + build_trailer(image, bench_signer, forged_cert, 3, "pico_w_test")
    # Sixteen env bytes and no terminator, signed for real: the format check, not the
    # signature, has to be what refuses it.
    env_full = image + build_trailer(image, signer, cert, 3, None, raw_env=b"pico_w_test_long")
    # Every genuine signature in these blobs, as (pub, digest, sig): the host's verify
    # answers true for exactly these, so a module that hashes the wrong bytes fails.
    valid = []
    for blob, root_pub in ((good, pub_bytes(root)), (bench, pub_bytes(bench_root)), (rogue_blob, pub_bytes(rogue)),
                           (forged, pub_bytes(bench_root)), (env_full, pub_bytes(root))):
        t = blob[-TRAILER_LEN:]
        c = t[24:161]
        valid.append((root_pub, cert_digest(c[:CERT_BODY_LEN]), c[73:137]))
        valid.append((c[8:73], image_digest(blob[:-TRAILER_LEN], t[:SIGNED_PREFIX_LEN]), t[161:225]))
    for pub, dig, sig in valid:
        assert verify_digest(pub, dig, sig), "a vector that does not verify"
    return dict(root_pub=pub_bytes(root), bench_root_pub=pub_bytes(bench_root), good=good, bench=bench,
                rogue=rogue_blob, forged=forged, env_full=env_full, image_len=len(image), valid=valid)


def c_array(name, data):
    lines = [f"static const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)


def cmd_vectors(a):
    v = build_vectors()
    parts = [
        "/* Generated by tools/ota_sign.py vectors — do not edit. A fixed TEST key set that no",
        " * image trusts: these bytes only drive test/test_ota_sig. Regenerate after a format change. */",
        "#pragma once",
        "#include <stdint.h>",
        "",
        c_array("kVecRootPub", v["root_pub"]),
        c_array("kVecBenchRootPub", v["bench_root_pub"]),
        f"static const uint32_t kVecImageLen = {v['image_len']};",
        c_array("kVecGood", v["good"]),
        c_array("kVecBench", v["bench"]),
        c_array("kVecRogue", v["rogue"]),
        c_array("kVecScopeForged", v["forged"]),
        c_array("kVecEnvFull", v["env_full"]),
        "struct VecSig { uint8_t pub[65]; uint8_t digest[32]; uint8_t sig[64]; };",
        "static const VecSig kVecValidSigs[] = {",
    ]
    for pub, dig, sig in v["valid"]:
        parts.append("    { {" + ", ".join(f"0x{b:02x}" for b in pub) + "},")
        parts.append("      {" + ", ".join(f"0x{b:02x}" for b in dig) + "},")
        parts.append("      {" + ", ".join(f"0x{b:02x}" for b in sig) + "} },")
    parts.append("};")
    text = "\n".join(parts) + "\n"
    if a.check:
        return check_vectors_file(a.out)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w") as f:
        f.write(text)
    print(f"{a.out}: {len(v['valid'])} signatures, image {v['image_len']} B")
    return 0


def parse_vectors_file(path):
    """The arrays of a committed vectors.h, back as bytes."""
    import re
    text = open(path).read()
    arrays = {m.group(1): bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", m.group(2)))
              for m in re.finditer(r"static const uint8_t (\w+)\[\d+\] = \{(.*?)\};", text, re.S)}
    sigs = []
    block = text.split("kVecValidSigs[] = {", 1)[1]
    for m in re.finditer(r"\{ \{(.*?)\},\s*\{(.*?)\},\s*\{(.*?)\} \}", block, re.S):
        sigs.append(tuple(bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", g)) for g in m.groups()))
    return arrays, sigs


def check_vectors_file(path):
    """ECDSA is randomized, so the committed vectors are checked by meaning, not byte for
    byte: every listed signature verifies, and each blob gets the verdict the tests expect."""
    arrays, sigs = parse_vectors_file(path)
    bad = [i for i, (pub, dig, sig) in enumerate(sigs) if not verify_digest(pub, dig, sig)]
    prod = [(arrays["kVecRootPub"], 1)]
    both = prod + [(arrays["kVecBenchRootPub"], 2)]
    verdicts = [check(arrays["kVecGood"], prod, {}, 0, "pico_w_test")[0] == V_OK,
                check(arrays["kVecBench"], both, {}, 0, "pico_w_test")[0] == V_OK,
                check(arrays["kVecBench"], prod, {}, 0, "pico_w_test")[0] == V_SCOPE,
                check(arrays["kVecRogue"], prod, {}, 0, "pico_w_test")[0] == V_INVALID,
                check(arrays["kVecScopeForged"], both, {}, 0, "pico_w_test")[0] == V_INVALID,
                check(arrays["kVecEnvFull"], prod, {}, 0, "pico_w_test")[0] == V_INVALID]
    ok = not bad and len(sigs) == 10 and all(verdicts)
    print(f"vectors: {len(sigs)} signatures, {len(bad)} bad; verdicts {verdicts}: {'OK' if ok else 'STALE'}")
    return 0 if ok else 1


def cmd_selftest(a):
    v = build_vectors()
    root_pub, bench_pub = v["root_pub"], v["bench_root_pub"]
    prod = [(root_pub, 1)]
    benchimg = [(root_pub, 1), (bench_pub, 2)]
    cases = []

    def case(name, blob, anchors, want, min_serial=None, min_sec=0, env="pico_w_test"):
        got, _ = check(blob, anchors, min_serial or {}, min_sec, env)
        cases.append((name, got == want, got, want))

    good = v["good"]
    n = len(good) - TRAILER_LEN

    def flip(blob, off):
        b = bytearray(blob)
        b[off] ^= 0x01
        return bytes(b)

    case("good", good, prod, V_OK)
    case("no trailer", good[:n], prod, V_MISSING)
    case("footer magic", flip(good, len(good) - 1), prod, V_MISSING)
    case("trailer_len", flip(good, len(good) - 16), prod, V_INVALID)
    case("format", flip(good, len(good) - 12), prod, V_INVALID)
    case("image byte", flip(good, 100), prod, V_INVALID)
    case("security_version", flip(good, n + 0), prod, V_INVALID)
    case("env", flip(good, n + 5), prod, V_INVALID)
    case("image_len", flip(good, n + 20), prod, V_INVALID)
    case("cert serial", flip(good, n + 24), prod, V_INVALID)
    case("cert pubkey", flip(good, n + 40), prod, V_INVALID)
    case("root signature", flip(good, n + 100), prod, V_INVALID)
    case("image signature", flip(good, n + 170), prod, V_INVALID)
    case("bench on production", v["bench"], prod, V_SCOPE)
    case("bench on bench", v["bench"], benchimg, V_OK)
    case("rogue root", v["rogue"], prod, V_INVALID)
    case("release scope from the bench root", v["forged"], benchimg, V_INVALID)
    case("env with no terminator", v["env_full"], prod, V_INVALID)
    case("bytes between image and trailer", good[:n] + b"\0" * 7 + good[n:], prod, V_INVALID)
    case("revoked", good, prod, V_REVOKED, min_serial={1: 6})
    case("serial at the minimum", good, prod, V_OK, min_serial={1: 5})
    case("rollback", good, prod, V_ROLLBACK, min_sec=4)
    case("security at the minimum", good, prod, V_OK, min_sec=3)
    case("env mismatch", good, prod, V_ENV, env="pico_w_release")
    case("extra byte after", good + b"\0", prod, V_MISSING)
    case("truncated", good[-100:], prod, V_MISSING)
    bad = [c for c in cases if not c[1]]
    for name, ok, got, want in cases:
        print(f"[{'ok' if ok else 'FAIL'}] {name}: v={got}" + ("" if ok else f" (wanted {want})"))
    print(f"{len(cases) - len(bad)}/{len(cases)} cases")
    return 1 if bad else 0


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("root-new"); s.add_argument("--out", required=True); s.add_argument("--pub-out", required=True)
    s = sub.add_parser("signer-new")
    s.add_argument("--root", required=True); s.add_argument("--scope", choices=SCOPES, required=True)
    s.add_argument("--serial", type=int, required=True)
    s.add_argument("--key-out", required=True); s.add_argument("--cert-out", required=True)
    s.add_argument("--no-encrypt", action="store_true", help="for a key that goes straight into a CI secret")
    s = sub.add_parser("sign")
    s.add_argument("--key", required=True); s.add_argument("--cert", required=True)
    s.add_argument("--security-version", type=int, required=True); s.add_argument("--env", required=True)
    s.add_argument("--in", dest="input", required=True); s.add_argument("--out", dest="output", required=True)
    s.add_argument("--root-pub", action="append", help="check the result against this root before writing it")
    s = sub.add_parser("verify")
    s.add_argument("--in", dest="input", required=True); s.add_argument("--env", required=True)
    s.add_argument("--root-pub", action="append"); s.add_argument("--bench-root-pub", action="append")
    s.add_argument("--min-serial", type=int, default=0); s.add_argument("--min-bench-serial", type=int, default=0)
    s.add_argument("--min-security", type=int, default=0)
    s = sub.add_parser("vectors"); s.add_argument("--out", default="test/test_ota_sig/vectors.h")
    s.add_argument("--check", action="store_true")
    sub.add_parser("selftest")
    a = p.parse_args()
    fn = {"root-new": cmd_root_new, "signer-new": cmd_signer_new, "sign": cmd_sign, "verify": cmd_verify,
          "vectors": cmd_vectors, "selftest": cmd_selftest}[a.cmd]
    sys.exit(fn(a) or 0)


if __name__ == "__main__":
    main()
