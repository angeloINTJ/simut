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

What an image trusts is compiled into it as a trust block (src/ota/ota_trust.h, written by
`gen-trust` from keys/):

  off  len  field
    0    8  magic b"SIMUTKEY"
    8    1  format (1)
    9    1  anchor count n, 1..4
   10    2  reserved, zero
   12    4  lowest security_version accepted        u32 LE
   16    4  lowest serial accepted, release scope   u32 LE
   20    4  lowest serial accepted, bench scope     u32 LE
   24  66n  anchors: scope (1 B), then the root's uncompressed P-256 point (65 B)

The device takes its policy from those bytes, and `sign` reads the same bytes out of the .bin
before it signs: what the tool checks is what the device will enforce once the image runs.

Subcommands
  root-new     a root key, encrypted with a passphrase typed here; writes the public key file
  signer-new   a signer key and its certificate, signed by a root
  gen-trust    src/ota/ota_trust.h from keys/ (--check: fail if it is stale)
  check-keys   keys/ holds together: the roots parse, the policy is sane, and every signer
               certificate there is signed by the root of its scope and not revoked by it
  sign         append the trailer to a .bin, after checking the image may carry it
  verify       check a signed .bin the way the device does
  inspect      what a .bin says it is, what it trusts, and how it is signed
  vectors      the host-test vectors (test/test_ota_sig/vectors.h), from a fixed test key
  selftest     round trips and tamper cases, with real ECDSA

The private keys never leave the files given; nothing here prints one.

Project: SIMUT. License: MIT.
"""

import argparse
import getpass
import hashlib
import json
import os
import re
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

TRUST_MAGIC = b"SIMUTKEY"
TRUST_FORMAT = 1
TRUST_HEAD_LEN = 24
ANCHOR_LEN = 66             # scope(1) pubkey(65)
MAX_ANCHORS = 4
ENV_TAG = re.compile(rb"SIMUT-ENV:([a-z]+);")   # src/BuildIdentity.cpp; the device reads [a-z] up to ';'

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEYS_DIR = os.path.join(ROOT, "keys")
TRUST_HEADER = os.path.join(ROOT, "src", "ota", "ota_trust.h")

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


# ── what an image is, and what it trusts ────────────────────────────────────

class SignRefused(Exception):
    """An image that must not carry the signature asked for. The message says why."""


def trust_block(anchors, security_version, lowest_serial):
    """anchors: [(root_pub_raw, scope)], in block order; lowest_serial: {scope: serial}."""
    if not 1 <= len(anchors) <= MAX_ANCHORS:
        raise ValueError(f"a trust block holds 1..{MAX_ANCHORS} roots")
    head = TRUST_MAGIC + struct.pack("<BBHIII", TRUST_FORMAT, len(anchors), 0, security_version,
                                     lowest_serial.get(SCOPES["release"], 0),
                                     lowest_serial.get(SCOPES["bench"], 0))
    return head + b"".join(bytes([scope]) + pub for pub, scope in anchors)


def parse_trust(data, off=0):
    """The trust block at data[off:] as {anchors, min_serial, min_security, length}, or None
    when the bytes there are not one. Stricter than the device, which takes its own block
    on faith past the shape: every root here must be a point on the curve."""
    if len(data) - off < TRUST_HEAD_LEN or data[off:off + len(TRUST_MAGIC)] != TRUST_MAGIC:
        return None
    fmt, n, rsv, secver, rel, bench = struct.unpack_from("<BBHIII", data, off + len(TRUST_MAGIC))
    if fmt != TRUST_FORMAT or not 1 <= n <= MAX_ANCHORS or rsv != 0:
        return None
    end = off + TRUST_HEAD_LEN + ANCHOR_LEN * n
    if end > len(data):
        return None
    anchors = []
    for i in range(n):
        a = off + TRUST_HEAD_LEN + ANCHOR_LEN * i
        scope, pub = data[a], bytes(data[a + 1:a + ANCHOR_LEN])
        if scope not in SCOPES.values():
            return None
        try:
            pub_from_bytes(pub)
        except ValueError:
            return None
        anchors.append((pub, scope))
    return dict(anchors=anchors, min_security=secver, length=end - off,
                min_serial={SCOPES["release"]: rel, SCOPES["bench"]: bench})


def image_trust(image):
    """The one trust block compiled into an image. The magic alone can appear by accident
    (the device keeps a copy to check its own block against), so only hits that parse
    count."""
    found, at = [], image.find(TRUST_MAGIC)
    while at >= 0:
        t = parse_trust(image, at)
        if t:
            found.append(t)
        at = image.find(TRUST_MAGIC, at + 1)
    if not found:
        raise SignRefused("the image has no trust block: installed, it would refuse every update "
                          "(a build from before signed OTA?)")
    if len(found) > 1:
        raise SignRefused(f"the image has {len(found)} trust blocks; which one the device uses "
                          "is not something to guess")
    return found[0]


def env_tag(image):
    """The env of the image's SIMUT-ENV tag, read the way the device reads it."""
    envs = {m.group(1).decode() for m in ENV_TAG.finditer(image)}
    if not envs:
        raise SignRefused("the image has no SIMUT-ENV tag: nothing says which variant it is for")
    if len(envs) > 1:
        raise SignRefused(f"the image has tags for {sorted(envs)}")
    return envs.pop()


def known_roots(keys_dir=KEYS_DIR):
    """{(root_pub_raw, scope)} that keys/ names: the only roots an image may trust."""
    return {(read_pub(os.path.join(keys_dir, "ota_root_release.pub")), SCOPES["release"]),
            (read_pub(os.path.join(keys_dir, "ota_root_bench.pub")), SCOPES["bench"])}


def read_cert(path):
    """A certificate file: the 137 bytes `signer-new` writes, or the same in hex, which is
    the form keys/ keeps so a review can read what changed."""
    raw = open(path, "rb").read()
    if len(raw) == CERT_LEN:
        return raw
    try:
        cert = bytes.fromhex(raw.decode("ascii").strip())
    except (UnicodeDecodeError, ValueError):
        cert = b""
    if len(cert) != CERT_LEN:
        raise ValueError(f"{path}: not a certificate ({CERT_LEN} bytes, or their hex)")
    return cert


def check_keys(keys_dir=KEYS_DIR):
    """What is wrong with keys/, as a list of lines; empty when it holds together. The CI
    signs with the certificate kept there, so a certificate from another root, or one
    the compiled policy already revokes, has to fail here rather than at a release."""
    problems = []
    roots = {}
    for scope, name in ((SCOPES["release"], "ota_root_release.pub"), (SCOPES["bench"], "ota_root_bench.pub")):
        try:
            roots[scope] = read_pub(os.path.join(keys_dir, name))
        except (OSError, ValueError) as e:
            problems.append(f"{name}: {e}")
    lowest = {}
    try:
        with open(os.path.join(keys_dir, "ota_policy.json"), encoding="utf-8") as f:
            policy = json.load(f)
        secver = policy["security_version"]
        if type(secver) is not int or secver < 1:
            problems.append("ota_policy.json: security_version must be an integer of 1 or more")
        for name, scope in SCOPES.items():
            v = policy["lowest_serial"][name]
            if type(v) is not int or v < 0:
                problems.append(f"ota_policy.json: lowest_serial.{name} must be a whole number")
            lowest[scope] = v
    except (OSError, ValueError, KeyError, TypeError) as e:
        problems.append(f"ota_policy.json: {e!r}")
    certs = sorted(n for n in os.listdir(keys_dir) if n.startswith("ota_signer_") and n.endswith(".cert"))
    for name in certs:
        try:
            cert = read_cert(os.path.join(keys_dir, name))
        except ValueError as e:
            problems.append(str(e))
            continue
        serial, scope = struct.unpack("<IB", cert[0:5])
        if scope not in roots or cert[5:8] != b"\0\0\0":
            problems.append(f"{name}: scope {scope} or reserved bytes are not a certificate's")
            continue
        try:
            pub_from_bytes(cert[8:73])
        except ValueError:
            problems.append(f"{name}: the signer key is not a point on the curve")
            continue
        if not verify_digest(roots[scope], cert_digest(cert[:CERT_BODY_LEN]), cert[73:137]):
            problems.append(f"{name}: not signed by the {'release' if scope == 1 else 'bench'} root in keys/")
        if serial < lowest.get(scope, 0):
            problems.append(f"{name}: serial {serial} is below the lowest the policy accepts "
                            f"({lowest.get(scope)}): images built now would refuse it")
    return problems, certs


# What the self-check of `sign` means when it fails: the image, once installed, would refuse
# the next image from this same signer. An update that ends the updates.
SELF_CHECK_WHY = {
    V_SCOPE: "the image trusts no root for this signer's scope",
    V_INVALID: "the image does not trust the root that certified this signer",
    V_REVOKED: "the image's lowest accepted serial is above this signer's",
    V_ROLLBACK: "the image's lowest accepted security_version is above the one signed",
}


def sign_image(image, signer_key, cert, roots, env=None, security_version=None):
    """The signed image, after the checks that keep a signature off an image that must not
    carry it. roots: known_roots( ). security_version: None takes the image's own floor, so
    an image never refuses itself; a lower one is for the rig's rollback case only."""
    if len(cert) != CERT_LEN:
        raise SignRefused("a certificate is 137 bytes")
    if pub_bytes(signer_key) != cert[8:73]:
        # A key rotated in one place and not the other (the CI secret, keys/): say so,
        # instead of letting the self-check below call it an untrusted root.
        raise SignRefused("the key is not the one this certificate certifies")
    if image[-len(MAGIC):] == MAGIC:
        raise SignRefused("the image is already signed")
    tag = env_tag(image)
    if env is not None and env != tag:
        raise SignRefused(f"--env {env}, but the image's tag says {tag}")
    trust = image_trust(image)
    stranger = [pub.hex()[:16] for pub, sc in trust["anchors"] if (pub, sc) not in roots]
    if stranger:
        raise SignRefused("the image trusts a root keys/ does not name: " + ", ".join(stranger))
    scope = cert[4]
    if scope == SCOPES["release"] and any(sc == SCOPES["bench"] for _, sc in trust["anchors"]):
        raise SignRefused("a release signature on an image that trusts the bench root would make "
                          "the bench key a way into any device that installs it; sign a "
                          "published profile, or use the bench signer")
    secver = trust["min_security"] if security_version is None else security_version
    blob = image + build_trailer(image, signer_key, cert, secver, tag)
    floor = trust["min_security"] if security_version is None else 0
    v, info = check(blob, trust["anchors"], trust["min_serial"], floor, tag)
    if v != V_OK:
        raise SignRefused(SELF_CHECK_WHY.get(v, f"v={v}") + ": installed, it would refuse the "
                          "next image from this signer")
    return blob, info


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
    passphrase = None if a.no_encrypt else read_passphrase("Passphrase for the new root: ", confirm=True)
    write_private(a.out, key, passphrase)
    write_pub(a.pub_out, pub_bytes(key))
    how = "NOT encrypted" if a.no_encrypt else "encrypted"
    print(f"root written to {a.out} ({how}); public key {a.pub_out}: {pub_bytes(key).hex()}")


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
    try:
        cert = read_cert(a.cert)
    except ValueError as e:
        sys.exit(str(e))
    try:
        roots = known_roots(a.keys)
        blob, info = sign_image(image, load_key(a.key), cert, roots, a.env, a.security_version)
    except SignRefused as e:
        sys.exit(f"{a.input}: not signed: {e}")
    if os.path.exists(a.output):
        sys.exit(f"{a.output} exists; nothing written")
    with open(a.output, "wb") as f:
        f.write(blob)
    scope = {v: k for k, v in SCOPES.items()}[info["scope"]]
    print(f"{a.output}: {len(image)} + {TRAILER_LEN} B, env {info['env']}, security_version "
          f"{info['security_version']}, {scope} signer serial {info['serial']}")


def cmd_gen_trust(a):
    text = trust_header(a.keys)
    if a.check:
        have = open(a.out, encoding="utf-8").read() if os.path.exists(a.out) else None
        if have != text:
            print(f"{os.path.relpath(a.out, ROOT)} is stale against keys/: run tools/ota_sign.py gen-trust")
            return 1
        print(f"{os.path.relpath(a.out, ROOT)} matches keys/")
        return 0
    with open(a.out, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"{os.path.relpath(a.out, ROOT)} written from keys/")
    return 0


def cmd_check_keys(a):
    problems, certs = check_keys(a.keys)
    for p in problems:
        print(f"keys/{p}")
    print(f"keys/: {len(certs)} signer certificate(s), {len(problems)} problem(s)")
    return 1 if problems else 0


def cmd_verify(a):
    blob = open(a.input, "rb").read()
    if a.running:
        # The verdict the device running that image would give: its trust block, its tag.
        running = open(a.running, "rb").read()
        if running[-len(MAGIC):] == MAGIC:
            running = running[:-TRAILER_LEN]
        try:
            trust, env = image_trust(running), env_tag(running)
        except SignRefused as e:
            sys.exit(f"{a.running}: {e}")
        v, info = check(blob, trust["anchors"], trust["min_serial"], trust["min_security"], env)
    else:
        if not a.env:
            sys.exit("verify needs --env, or --running to take it from an image")
        anchors = [(read_pub(p), SCOPES["release"]) for p in (a.root_pub or [])]
        anchors += [(read_pub(p), SCOPES["bench"]) for p in (a.bench_root_pub or [])]
        min_serial = {SCOPES["release"]: a.min_serial, SCOPES["bench"]: a.min_bench_serial}
        v, info = check(blob, anchors, min_serial, a.min_security, a.env)
    print(f"v={v} {info}")
    return 0 if v == V_OK else 1


def cmd_inspect(a):
    image = open(a.input, "rb").read()
    signed = len(image) > TRAILER_LEN and image[-len(MAGIC):] == MAGIC
    body = image[:-TRAILER_LEN] if signed else image
    names = {}
    try:
        for pub, sc in known_roots(a.keys):
            names[pub] = "keys/ota_root_release.pub" if sc == SCOPES["release"] else "keys/ota_root_bench.pub"
    except OSError:
        pass   # inspect still works outside the repository; it just cannot name the roots
    scope_name = {v: k for k, v in SCOPES.items()}
    for what, fn in (("env", env_tag), ("trust", image_trust)):
        try:
            got = fn(body)
        except SignRefused as e:
            print(f"{what}: {e}")
            continue
        if what == "env":
            print(f"env: {got}")
        else:
            print(f"trust: lowest security_version {got['min_security']}, lowest serial release "
                  f"{got['min_serial'][1]}, bench {got['min_serial'][2]}")
            for pub, sc in got["anchors"]:
                print(f"  {scope_name.get(sc, sc)} root {pub.hex()[:16]}... "
                      f"{names.get(pub, '(not in keys/)')}")
    if not signed:
        print("signature: none")
        return 0
    t = image[-TRAILER_LEN:]
    secver, = struct.unpack("<I", t[0:4])
    serial, sc = struct.unpack("<IB", t[24:29])
    env = t[4:20].split(b"\0", 1)[0].decode("ascii", "replace")
    print(f"signature: {len(body)} B signed for env {env}, security_version {secver}, "
          f"{scope_name.get(sc, sc)} signer serial {serial} ({t[32:40].hex()}...)")
    return 0


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


def trust_header(keys_dir):
    """src/ota/ota_trust.h: the two trust blocks an image can carry, from keys/."""
    release = read_pub(os.path.join(keys_dir, "ota_root_release.pub"))
    bench = read_pub(os.path.join(keys_dir, "ota_root_bench.pub"))
    with open(os.path.join(keys_dir, "ota_policy.json"), encoding="utf-8") as f:
        policy = json.load(f)
    secver = int(policy["security_version"])
    lowest = {SCOPES[k]: int(v) for k, v in policy["lowest_serial"].items()}
    rel = trust_block([(release, SCOPES["release"])], secver, lowest)
    both = trust_block([(release, SCOPES["release"]), (bench, SCOPES["bench"])], secver, lowest)
    return "\n".join([
        "/* GENERATED by tools/ota_sign.py gen-trust from keys/ - do not edit: change keys/ and",
        " * run it again. CI runs `gen-trust --check`.",
        " *",
        " * The trust block an image carries: the roots it accepts the next image from and the",
        " * floors it holds that image to (format in tools/ota_sign.py). validation.cpp takes",
        " * its policy from these bytes, and `ota_sign.py sign` reads the same bytes out of a",
        " * .bin before it signs, so what the tool checks is what the device enforces.",
        " *",
        f" * keys/ota_policy.json: security_version {secver}; lowest serial release "
        f"{lowest.get(SCOPES['release'], 0)}, bench {lowest.get(SCOPES['bench'], 0)}.",
        f" * release root {release.hex()[:16]}..., bench root {bench.hex()[:16]}...",
        " */",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "namespace ota {",
        "",
        "/* A published image trusts the release root only. */",
        c_array("kTrustRelease", rel),
        "",
        "/* A bench image (SIMUT_OTA_TRUST_BENCH, which only an unpublished profile in",
        " * tools/features.toml may set) trusts the bench root as well. */",
        c_array("kTrustBench", both),
        "",
        "}  // namespace ota",
        "",
    ])


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
        cases.append((name, got == want, f"v={got}", f"v={want}"))

    def expect(name, ok, got="", want=""):
        cases.append((name, bool(ok), got or ("ok" if ok else "no"), want))

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

    # The trust block: the format both sides read.
    low = {1: 2, 2: 7}
    blk = trust_block([(root_pub, 1), (bench_pub, 2)], 4, low)
    t = parse_trust(blk)
    expect("trust block round trip", t == dict(anchors=[(root_pub, 1), (bench_pub, 2)], min_security=4,
                                               min_serial=low, length=TRUST_HEAD_LEN + 2 * ANCHOR_LEN))

    def mangled(off, value):
        b = bytearray(blk)
        b[off] = value
        return bytes(b)

    expect("trust: magic", parse_trust(mangled(0, ord("X"))) is None)
    expect("trust: format", parse_trust(mangled(8, 2)) is None)
    expect("trust: no roots", parse_trust(mangled(9, 0)) is None)
    expect("trust: more roots than bytes", parse_trust(mangled(9, 3)) is None)
    expect("trust: reserved", parse_trust(mangled(10, 1)) is None)
    expect("trust: scope 3", parse_trust(mangled(TRUST_HEAD_LEN, 3)) is None)
    expect("trust: a point off the curve", parse_trust(mangled(TRUST_HEAD_LEN + 40, blk[TRUST_HEAD_LEN + 40] ^ 1)) is None)

    # `sign`: what may carry which signature. A synthetic image carries what a real one
    # does: one env tag and one trust block.
    root, bench_root, rogue = test_key(TEST_ROOT), test_key(TEST_BENCH_ROOT), test_key(TEST_ROGUE_ROOT)
    signer, bench_signer = test_key(TEST_SIGNER), test_key(TEST_BENCH_SIGNER)
    roots = {(root_pub, 1), (bench_pub, 2)}
    floors = {1: 5, 2: 2}
    rel_trust = trust_block([(root_pub, 1)], 3, floors)
    bench_trust = trust_block([(root_pub, 1), (bench_pub, 2)], 3, floors)

    def image(trust=rel_trust, tag=b"SIMUT-ENV:release;v=0.0.0;"):
        return test_image() + tag + trust + test_image(64)

    rel_cert = make_cert(root, pub_bytes(signer), 5, 1)
    bench_cert = make_cert(bench_root, pub_bytes(bench_signer), 2, 2)

    def signs(name, img, key, cert, want_ok, **kw):
        try:
            blob, _ = sign_image(img, key, cert, roots, **kw)
        except SignRefused as e:
            expect(name, not want_ok, f"refused: {e}")
            return None
        expect(name, want_ok, "signed")
        return blob

    blob = signs("sign: release key, release image", image(), signer, rel_cert, True)
    if blob:
        expect("sign: it verifies as the device running it would",
               check(blob, [(root_pub, 1)], floors, 3, "release")[0] == V_OK)
        expect("sign: security_version is the image's own floor", blob[-TRAILER_LEN:][0:4] == struct.pack("<I", 3))
        expect("sign: env from the tag", blob[-TRAILER_LEN:][4:20].rstrip(b"\0") == b"release")
    signs("sign: bench key, bench image", image(bench_trust), bench_signer, bench_cert, True)
    signs("sign: release key on a bench-trusting image", image(bench_trust), signer, rel_cert, False)
    signs("sign: bench key on a release image", image(), bench_signer, bench_cert, False)
    signs("sign: a signer from a root the image does not trust", image(), signer,
          make_cert(rogue, pub_bytes(signer), 5, 1), False)
    signs("sign: a signer below the image's own lowest serial", image(), signer,
          make_cert(root, pub_bytes(signer), 4, 1), False)
    signs("sign: an image trusting a root keys/ does not name", image(trust_block([(pub_bytes(rogue), 1)], 3, floors)),
          signer, make_cert(rogue, pub_bytes(signer), 5, 1), False)
    signs("sign: no env tag", image(tag=b""), signer, rel_cert, False)
    signs("sign: two env tags", image(tag=b"SIMUT-ENV:release;v=1;SIMUT-ENV:air;v=1;"), signer, rel_cert, False)
    signs("sign: --env that is not the tag", image(), signer, rel_cert, False, env="air")
    signs("sign: no trust block", image(trust=b""), signer, rel_cert, False)
    signs("sign: two trust blocks", image(trust=rel_trust + rel_trust), signer, rel_cert, False)
    signs("sign: already signed", blob or b"", signer, rel_cert, False)
    low_blob = signs("sign: a lower security_version, asked for", image(), signer, rel_cert, True, security_version=2)
    if low_blob:
        expect("sign: which the image it came from refuses as a rollback",
               check(low_blob, [(root_pub, 1)], floors, 3, "release")[0] == V_ROLLBACK)

    # gen-trust: the header's arrays are the blocks, and the C++ side parses the same bytes
    # (test/test_ota_sig). Written from a scratch keys/ with the TEST roots.
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        write_pub(os.path.join(d, "ota_root_release.pub"), root_pub)
        write_pub(os.path.join(d, "ota_root_bench.pub"), bench_pub)
        with open(os.path.join(d, "ota_policy.json"), "w") as f:
            json.dump({"security_version": 3, "lowest_serial": {"release": 5, "bench": 2}}, f)
        text = trust_header(d)
    arrays = {m.group(1): bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", m.group(2)))
              for m in re.finditer(r"static const uint8_t (\w+)\[\d+\] = \{(.*?)\};", text, re.S)}
    expect("gen-trust: the release block", arrays.get("kTrustRelease") == rel_trust)
    expect("gen-trust: the bench block", arrays.get("kTrustBench") == bench_trust)

    # A key and a certificate that do not belong together (a rotation done in the CI
    # secret and not in keys/, or the other way) are named as such.
    try:
        sign_image(image(), bench_signer, rel_cert, roots)
        expect("sign: a key that is not the certificate's", False, "signed")
    except SignRefused as e:
        expect("sign: a key that is not the certificate's", "not the one this certificate" in str(e), f"refused: {e}")

    # Certificates: keys/ keeps them in hex; signer-new writes the 137 bytes.
    with tempfile.TemporaryDirectory() as d:
        binp, hexp, badp = (os.path.join(d, n) for n in ("c.bin", "c.hex", "c.bad"))
        open(binp, "wb").write(rel_cert)
        open(hexp, "w").write(rel_cert.hex() + "\n")
        open(badp, "w").write(rel_cert.hex()[:-2] + "\n")
        expect("certificate: binary and hex read the same", read_cert(binp) == read_cert(hexp) == rel_cert)
        try:
            read_cert(badp)
            expect("certificate: a short one is refused", False)
        except ValueError:
            expect("certificate: a short one is refused", True)

    # check-keys: what the CI signs with has to be what keys/ vouches for.
    def keys_dir(d, certs, lowest_release=1):
        write_pub(os.path.join(d, "ota_root_release.pub"), root_pub)
        write_pub(os.path.join(d, "ota_root_bench.pub"), bench_pub)
        with open(os.path.join(d, "ota_policy.json"), "w") as f:
            json.dump({"security_version": 1, "lowest_serial": {"release": lowest_release, "bench": 1}}, f)
        for name, c in certs.items():
            open(os.path.join(d, name), "w").write(c.hex() + "\n")

    with tempfile.TemporaryDirectory() as d:
        keys_dir(d, {"ota_signer_release.cert": rel_cert, "ota_signer_bench.cert": bench_cert})
        problems, certs = check_keys(d)
        expect("check-keys: a sound keys/", not problems and len(certs) == 2, f"{problems}")
    with tempfile.TemporaryDirectory() as d:
        keys_dir(d, {"ota_signer_release.cert": make_cert(bench_root, pub_bytes(signer), 5, 1)})
        expect("check-keys: a release certificate from the bench root", len(check_keys(d)[0]) == 1)
    with tempfile.TemporaryDirectory() as d:
        keys_dir(d, {"ota_signer_release.cert": rel_cert}, lowest_release=6)
        expect("check-keys: a certificate the policy revokes", len(check_keys(d)[0]) == 1)
    with tempfile.TemporaryDirectory() as d:
        keys_dir(d, {})
        open(os.path.join(d, "ota_signer_release.cert"), "w").write("not hex\n")
        expect("check-keys: a file that is not a certificate", len(check_keys(d)[0]) == 1)
    bad = [c for c in cases if not c[1]]
    for name, ok, got, want in cases:
        print(f"[{'ok' if ok else 'FAIL'}] {name}: {got}" + ("" if ok or not want else f" (wanted {want})"))
    print(f"{len(cases) - len(bad)}/{len(cases)} cases")
    return 1 if bad else 0


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("root-new"); s.add_argument("--out", required=True); s.add_argument("--pub-out", required=True)
    s.add_argument("--no-encrypt", action="store_true",
                   help="the bench root only, which signs nothing a field device accepts")
    s = sub.add_parser("signer-new")
    s.add_argument("--root", required=True); s.add_argument("--scope", choices=SCOPES, required=True)
    s.add_argument("--serial", type=int, required=True)
    s.add_argument("--key-out", required=True); s.add_argument("--cert-out", required=True)
    s.add_argument("--no-encrypt", action="store_true", help="for a key that goes straight into a CI secret")
    s = sub.add_parser("gen-trust"); s.add_argument("--keys", default=KEYS_DIR)
    s.add_argument("--out", default=TRUST_HEADER); s.add_argument("--check", action="store_true")
    s = sub.add_parser("check-keys"); s.add_argument("--keys", default=KEYS_DIR)
    s = sub.add_parser("sign")
    s.add_argument("--key", required=True); s.add_argument("--cert", required=True)
    s.add_argument("--in", dest="input", required=True); s.add_argument("--out", dest="output", required=True)
    s.add_argument("--env", help="refuse unless the image's SIMUT-ENV tag says this")
    s.add_argument("--security-version", type=int,
                   help="default: the image's own floor. Lower only to stage a rollback on the rig")
    s.add_argument("--keys", default=KEYS_DIR, help="the roots an image may trust")
    s = sub.add_parser("verify")
    s.add_argument("--in", dest="input", required=True)
    s.add_argument("--running", help="the image the device runs: its trust block and its tag decide")
    s.add_argument("--env")
    s.add_argument("--root-pub", action="append"); s.add_argument("--bench-root-pub", action="append")
    s.add_argument("--min-serial", type=int, default=0); s.add_argument("--min-bench-serial", type=int, default=0)
    s.add_argument("--min-security", type=int, default=0)
    s = sub.add_parser("inspect"); s.add_argument("--in", dest="input", required=True)
    s.add_argument("--keys", default=KEYS_DIR)
    s = sub.add_parser("vectors"); s.add_argument("--out", default="test/test_ota_sig/vectors.h")
    s.add_argument("--check", action="store_true")
    sub.add_parser("selftest")
    a = p.parse_args()
    fn = {"root-new": cmd_root_new, "signer-new": cmd_signer_new, "gen-trust": cmd_gen_trust,
          "check-keys": cmd_check_keys,
          "sign": cmd_sign, "verify": cmd_verify, "inspect": cmd_inspect,
          "vectors": cmd_vectors, "selftest": cmd_selftest}[a.cmd]
    sys.exit(fn(a) or 0)


if __name__ == "__main__":
    main()
