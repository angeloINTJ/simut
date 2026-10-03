#!/usr/bin/env python3
"""Fails the build when a firmware image grows past its budget in tools/flash_budget.json.

WHY THIS EXISTS
---------------
The linker already refuses an image that does not fit. What it cannot do is
make growth visible while there is still room, and that is where this project
gets hurt: the SIMUT Air image has the least headroom of the five that link,
CI compiled only pico_w_release until 2026-09-08, and a change that ate several
kilobytes of the Air slot would have gone in green. This gate turns "it still
fits" into "it still fits, and here is what it cost".

WHAT IT MEASURES
----------------
The number PlatformIO prints, parsed out of a captured build log:

    Flash: [==========]  97.6% (used 1019276 bytes from 1044480 bytes)

Deliberately NOT a size recomputed here from the ELF. The measurement can be
reproduced from sections (.boot2 + .text + .rodata + .data comes out to exactly
the same figure today), but a gate that reimplements the toolchain's arithmetic
is a gate that can drift away from what the developer sees in their terminal
and then argue with them about it. Parsing the real line means the gate and the
build can never disagree; if the format ever changes, this fails loudly with
"no Flash: line" rather than quietly measuring the wrong thing.

THE OTA HEADROOM RECORD (2026-10-02)
-----------------------------------
The OTA ceiling does not move on this board — the staging area is the LittleFS
partition itself — so how far each image is from it is what is left for new
features. Each image's firmware.bin size is recorded as "bin" in
tools/flash_budget.json, and docs/analysis/PLANO_STABLE.md keeps the headroom
table that follows from it. Two checks hold them true:

  * after a build, a firmware.bin larger than its "bin" fails: an image that
    grows records its new size, and the table, in the same change;
  * --table (the CI gates job) compares the table, row for row, with what the
    record makes of it.

An image listed under "ota_exempt" is held to neither, and its row says so.
An image with a "slot" updates into the other slot of a partition table (the
RP2350 since step 4 of docs/analysis/OTA_AB_RP2350.md): its ceiling over the
air is that slot, and its row measures the margin against it.

USAGE
-----
    pio run -e pico_w_air 2>&1 | tee build.log
    python3 tools/check_flash_budget.py pico_w_air build.log

    # or read the log on stdin
    pio run -e pico_w_air 2>&1 | python3 tools/check_flash_budget.py pico_w_air -

    # the OTA headroom table against the record, no build needed
    python3 tools/check_flash_budget.py --table

Exit status is 0 when the image is within budget, 1 when it is over or when the
log carries no size line to check.

Project: SIMUT
License: MIT
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUDGET_FILE = os.path.join(ROOT, "tools", "flash_budget.json")
HEADROOM_DOC = os.path.join(ROOT, "docs", "analysis", "PLANO_STABLE.md")
HEADROOM_BEGIN = "<!-- ota-headroom:begin -->"
HEADROOM_END = "<!-- ota-headroom:end -->"
EXEMPT_CELL = "isenta"

# The whole contract with PlatformIO, in one place.
FLASH_RE = re.compile(r"used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes")


def ota_safe_max():
    """OTA_APP_SAFE_MAX_SIZE, read out of src/ota/ota_layout.h.

    Recomputed from the same #defines the firmware compiles, and not copied
    here, for the reason check_lang_packs gives about the pack limits: a gate
    holding its own copy of a number is a gate that can disagree with the code
    it guards. Returns None if the header stops looking like this, and the
    caller then says so instead of pretending to check."""
    with open(os.path.join(ROOT, "src", "ota", "ota_layout.h"),
              encoding="utf-8") as fh:
        src = fh.read()

    def define(name):
        m = re.search(rf"#define\s+{name}\s+(.+)", src)
        return m.group(1) if m else None

    try:
        total = eval(re.sub(r"[u]\b", "", define("OTA_FLASH_TOTAL").split("/*")[0]))
        eeprom = eval(re.sub(r"[u]\b", "", define("OTA_EEPROM_RESERVED").split("/*")[0]))
        fs = eval(re.sub(r"[u]\b", "", define("OTA_FILESYSTEM_SIZE").split("/*")[0]))
        sector = int(re.sub(r"[u]\b", "", define("OTA_FLASH_SECTOR_SIZE").split("/*")[0]).strip())
    except Exception:
        return None
    return total - eeprom - fs - sector


def sig_trailer_len():
    """SIG_TRAILER_LEN, read out of src/ota/signature.h for the same reason
    ota_safe_max( ) reads its numbers: no copy here to drift from the code.
    0 if the header stops looking like this, which check_ota_bin reports."""
    with open(os.path.join(ROOT, "src", "ota", "signature.h"), encoding="utf-8") as fh:
        m = re.search(r"constexpr\s+uint32_t\s+SIG_TRAILER_LEN\s*=\s*(\d+)\s*;", fh.read())
    return int(m.group(1)) if m else 0


def ota_bin_max():
    """The largest firmware.bin that still fits under the OTA ceiling once the
    signature trailer is appended: what the configurator and build_custom.py
    compare a .bin against. None if either number cannot be read."""
    safe, trailer = ota_safe_max(), sig_trailer_len()
    return safe - trailer if safe and trailer else None


def ota_ceiling(env_cfg, safe_max):
    """The size an image may have over the air, signature included: the Pico
    W's OTA_APP_SAFE_MAX_SIZE, or, for a chip that updates into the other slot
    of a partition table, that slot ("slot"; the stage refuses more, with
    OTA_IMAGE_MAX in src/ota/ota_layout.h)."""
    return env_cfg.get("slot") or safe_max


def held_ceiling(env_cfg, printed):
    """The size an image is held to. PlatformIO prints the sketch area its
    builder computes, flash minus LittleFS and EEPROM. A chip that boots from a
    slot of a partition table holds its program to the slot instead, recorded
    as "slot" (the RP2350: 1,568,768 B, tools/rp2350/partition_table.json),
    and the slot's linker script is that long too."""
    return env_cfg.get("slot") or printed


def table_int(cell):
    """A number as the table writes it — 1.028.493 (pt-BR), 1,028,493 or bare,
    negative for an image over the ceiling — or None for anything else."""
    digits = re.sub(r"[.,\s]", "", cell).replace("\u2212", "-")
    return int(digits) if re.fullmatch(r"-?\d+", digits) else None


def pt_int(n):
    """1028493 -> 1.028.493, the way the table (pt-BR) writes it."""
    return f"{n:,}".replace(",", ".")


def headroom_rows(cfg, safe_max, trailer):
    """{env: the row the record makes}, ready to paste into the table — what
    --table asks for. An image with no "bin" and no exemption has none."""
    exempt = cfg.get("ota_exempt", {})
    rows = {}
    for env, entry in cfg["envs"].items():
        if env in exempt:
            rows[env] = f"| `{env}` | {EXEMPT_CELL} | — |"
        elif "bin" in entry:
            signed = entry["bin"] + trailer
            room = ota_ceiling(entry, safe_max) - signed
            rows[env] = f"| `{env}` | {pt_int(signed)} | {pt_int(room)} |"
    return rows


def parse_headroom_table(text):
    """{env: (signed .bin, headroom)} from the rows between the markers; None
    for a row that says the image is exempt. ValueError without the markers."""
    a, b = text.find(HEADROOM_BEGIN), text.find(HEADROOM_END)
    if a < 0 or b < a:
        raise ValueError("no headroom table")
    rows = {}
    for line in text[a:b].splitlines():
        m = re.match(r"\|\s*`([a-z0-9_]+)`\s*\|\s*([^|]*?)\s*\|\s*([^|]*?)\s*\|", line)
        if not m:
            continue
        env, signed, room = m.groups()
        rows[env] = None if signed == EXEMPT_CELL else (table_int(signed), table_int(room))
    return rows


def headroom_table_errors(text, cfg, safe_max, trailer):
    """What the table gets wrong against tools/flash_budget.json; [] when it
    says, row for row, what the record makes of it."""
    try:
        rows = parse_headroom_table(text)
    except ValueError:
        return [f"no {HEADROOM_BEGIN} ... {HEADROOM_END} table in "
                f"docs/analysis/PLANO_STABLE.md"]
    exempt = cfg.get("ota_exempt", {})
    want_rows = headroom_rows(cfg, safe_max, trailer)
    errs = []
    for env, entry in cfg["envs"].items():
        if env not in exempt and "bin" not in entry:
            errs.append(f"{env}: no \"bin\" recorded in tools/flash_budget.json")
            continue
        want = want_rows[env]
        if env not in rows:
            errs.append(f"{env}: no row in the OTA headroom table; it should read  {want}")
            continue
        got = rows[env]
        if env in exempt:
            if got is not None:
                errs.append(f"{env}: exempt from the OTA ceiling in "
                            f"tools/flash_budget.json, so its row reads  {want}")
            continue
        signed = entry["bin"] + trailer
        ceiling = ota_ceiling(entry, safe_max)
        if got != (signed, ceiling - signed):
            errs.append(f"{env}: the row disagrees with \"bin\" {entry['bin']} "
                        f"(+ {trailer} B of signature, under {ceiling}); it "
                        f"should read  {want}")
    for env in rows:
        if env not in cfg["envs"]:
            errs.append(f"{env}: a row for an image with no budget")
    return errs


def bin_vs_record(actual, recorded):
    """(fails, message) for a built firmware.bin against its recorded size."""
    if recorded is None:
        return True, (f"no \"bin\" recorded for it in tools/flash_budget.json — "
                      f"record {actual} there (the size of firmware.bin), and its "
                      f"row in the OTA headroom table of docs/analysis/PLANO_STABLE.md.")
    if actual > recorded:
        return True, (f"firmware.bin is {actual} B, {actual - recorded} B more than "
                      f"tools/flash_budget.json records for it (\"bin\": {recorded}). "
                      f"An image that grows records its new size there, and its row "
                      f"in the OTA headroom table of docs/analysis/PLANO_STABLE.md "
                      f"(check_flash_budget.py --table), in this same change.")
    if actual < recorded:
        return False, (f"firmware.bin is {actual} B, {recorded - actual} B less than "
                       f"recorded: the OTA headroom table understates what is left. "
                       f"Record the new size when convenient.")
    return False, None


def check_ota_bin(env, ceiling, exempt=None, recorded_bin=None):
    """The budget above measures what PlatformIO prints, which is the SUM OF
    SECTIONS. What OTA refuses is the .bin, and the two differ by the padding
    the linker puts before .data's load address — so the .bin moves in 4 KiB
    steps and can cross the OTA ceiling while `used` still looks comfortable.
    This is the check that catches it, and it is the failure the budget number
    cannot see: the device refuses an image over OTA_APP_SAFE_MAX_SIZE — the
    stage stops short of the config snapshot's sectors, validation answers
    SIZE_TOO_LARGE, the applier will not copy it (src/ota/) — so it installs
    only over USB, and a fleet on it stops updating over the air. @p ceiling
    is ota_ceiling( )'s: the RP2350's is its slot, which the stage holds the
    same way (OTA_IMAGE_MAX)."""
    if exempt:
        print(f"[flash-budget] SKIP {env}: not held to the OTA ceiling on "
              f"purpose — {exempt}")
        return
    path = os.path.join(ROOT, ".pio", "build", env, "firmware.bin")
    if not os.path.exists(path):
        print(f"[flash-budget] NOTE {env}: no firmware.bin next to the log, "
              f"so the OTA ceiling was not checked.")
        return
    # What goes over the air is the .bin with the signature trailer after it
    # (docs/analysis/OTA_ASSINADA.md), so that is what has to fit.
    trailer = sig_trailer_len()
    if not trailer:
        fail("src/ota/signature.h no longer says SIG_TRAILER_LEN; the OTA "
             "ceiling cannot be checked against what is staged.")
    raw = os.path.getsize(path)
    size = raw + trailer
    slack = ceiling - size
    if slack < 0:
        fail(f"{env}: firmware.bin + {trailer} B of signature is {size} B, "
             f"the OTA ceiling is {ceiling} B — over by {-slack} B. The "
             f"device refuses an image this large (stage, validation, applier): "
             f"it would install only over USB.")
    band = 4096
    if slack < band:
        print(f"[flash-budget] WARN {env}: firmware.bin signed is {size} B, only "
              f"{slack} B under the OTA ceiling of {ceiling} B — less than one "
              f"flash sector. The next addition may make this image refuse to "
              f"update over the air while every other number still looks fine.")
    else:
        print(f"[flash-budget] OK {env}: firmware.bin signed {size} B, "
              f"{slack} B under the OTA ceiling.")
    fails, msg = bin_vs_record(raw, recorded_bin)
    if msg:
        msg += f" Its row would read  | `{env}` | {pt_int(size)} | {pt_int(slack)} |"
        if fails:
            fail(f"{env}: {msg}")
        print(f"[flash-budget] NOTE {env}: {msg}")


def check_table():
    """--table: the OTA headroom table of docs/analysis/PLANO_STABLE.md against
    tools/flash_budget.json. No build needed: the CI gates job runs it."""
    with open(BUDGET_FILE, encoding="utf-8") as fh:
        cfg = json.load(fh)
    safe, trailer = ota_safe_max(), sig_trailer_len()
    if not safe or not trailer:
        fail("could not read OTA_APP_SAFE_MAX_SIZE or SIG_TRAILER_LEN out of "
             "src/ota/ — the headroom table cannot be checked.")
    with open(HEADROOM_DOC, encoding="utf-8") as fh:
        errs = headroom_table_errors(fh.read(), cfg, safe, trailer)
    if errs:
        fail("the OTA headroom table and tools/flash_budget.json disagree:\n  "
             + "\n  ".join(errs))
    print(f"[flash-budget] OK the OTA headroom table matches the record "
          f"({len(cfg['envs'])} images, {len(cfg.get('ota_exempt', {}))} exempt).")


def fail(msg):
    print(f"[flash-budget] FAIL {msg}")
    sys.exit(1)


def main():
    if sys.argv[1:] == ["--table"]:
        check_table()
        return
    if len(sys.argv) != 3:
        print(__doc__.strip().split("USAGE\n-----\n", 1)[1])
        sys.exit(2)

    env, log_path = sys.argv[1], sys.argv[2]

    with open(BUDGET_FILE, encoding="utf-8") as fh:
        cfg = json.load(fh)

    text = sys.stdin.read() if log_path == "-" else \
        open(log_path, encoding="utf-8", errors="replace").read()

    # An environment with no budget is not silently waved through: it is either
    # known-unbudgeted (and named as such, with a reason) or the budget file has
    # fallen behind the environment list in platformio.ini.
    if env not in cfg["envs"]:
        why = cfg.get("unbudgeted", {}).get(env)
        if why:
            print(f"[flash-budget] SKIP {env}: no budget on purpose — {why}")
            return
        fail(f"{env} has no budget in tools/flash_budget.json. "
             f"Add one (measure with `pio run -e {env}`) or record it under "
             f"\"unbudgeted\" with the reason.")

    # The last match wins: a build log can carry sizes from more than one pass.
    matches = FLASH_RE.findall(text)
    if not matches:
        fail(f"{env}: no 'used N bytes from M bytes' line in {log_path}. "
             f"Either the build did not get as far as linking, or PlatformIO "
             f"changed the line this gate reads.")

    used, ceiling = (int(x) for x in matches[-1])
    budget = cfg["envs"][env]["budget"]
    # An environment on another chip declares its own slot (the RP2350's 4 MB
    # flash leaves 3 MB for code next to the same 1 MB of LittleFS); without
    # it, the note below would call a different board a moved layout.
    declared_ceiling = cfg["envs"][env].get("ceiling", cfg.get("ceiling"))

    # The ceiling moving is worth a word even when the image fits: it means the
    # partition layout changed under us, and every budget below was set against
    # the old one.
    if declared_ceiling and ceiling != declared_ceiling:
        print(f"[flash-budget] NOTE {env}: slot is {ceiling} B, but "
              f"flash_budget.json records a ceiling of {declared_ceiling} B — "
              f"the layout moved, so re-measure the budgets.")

    ceiling = held_ceiling(cfg["envs"][env], ceiling)
    if used > ceiling:
        fail(f"{env}: {used} B used, and its slot holds {ceiling} B.")

    delta = used - budget
    pct = 100.0 * used / ceiling

    if delta > 0:
        fail(f"{env}: {used} B used, budget is {budget} B — over by {delta} B "
             f"({pct:.1f}% of the {ceiling} B slot; {ceiling - used} B of real "
             f"headroom left).\n"
             f"           If the growth is intended, raise the budget in "
             f"tools/flash_budget.json in this same change and say why in the "
             f"commit message.")

    print(f"[flash-budget] OK {env}: {used} B used, {-delta} B under budget "
          f"({pct:.1f}% of slot, {ceiling - used} B to the ceiling)")

    safe_max = ota_safe_max()
    if safe_max is None:
        print(f"[flash-budget] NOTE {env}: could not read OTA_APP_SAFE_MAX_SIZE "
              f"out of src/ota/ota_layout.h — the OTA ceiling was not checked.")
    else:
        check_ota_bin(env, ota_ceiling(cfg["envs"][env], safe_max),
                      cfg.get("ota_exempt", {}).get(env),
                      cfg["envs"][env].get("bin"))


if __name__ == "__main__":
    main()
