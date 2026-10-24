#!/usr/bin/env python3
"""Tests the OTA headroom record of tools/check_flash_budget.py.

Run: python3 tools/test_flash_budget.py   (CI runs it as a gate)

The OTA ceiling (1,040,384 B with the signature) is the one number on this
board that does not move: the staging area is the LittleFS partition itself.
docs/analysis/PLANO_STABLE.md keeps a table of how far each image is from it,
and a table like that is right on the day it is typed. These pin the two rules
that keep it true:

  * a built firmware.bin larger than its recorded size ("bin" in
    tools/flash_budget.json) fails — an image that grows records it;
  * the table says, row for row, what the record makes of it — so neither can
    move without the other.

And one rule of the gate itself: when it cannot look (no firmware.bin, or an
ota_layout.h it cannot read), it fails instead of passing.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_flash_budget as cfb             # noqa: E402

SAFE = 1040384
TRAILER = 241
FAILS = []
RAN = []


def check(name, cond, detail=""):
    RAN.append(name)
    tag = "ok  " if cond else "FAIL"
    print(f"[{tag}] {name}" + (f" — {detail}" if detail and not cond else ""))
    if not cond:
        FAILS.append(name)


CFG = {
    "envs": {
        "pico_w_release": {"budget": 1, "measured": 1, "bin": 1028252},
        "pico_w_alpha": {"budget": 1, "measured": 1, "bin": 986284},
        "pico_w_test_https": {"budget": 1, "measured": 1},
    },
    "ota_exempt": {"pico_w_test_https": "bench image, flashed over USB"},
}


# The ceiling an image is held to: the slot, when the chip boots from one.
check("an image with a slot is held to the slot, not to the sketch area printed",
      cfb.held_ceiling({"budget": 1, "slot": 1568768}, 3145728) == 1568768)
check("an image without a slot is held to what PlatformIO prints",
      cfb.held_ceiling({"budget": 1}, 1044480) == 1044480)


def doc(*rows):
    return ("text before\n\n" + cfb.HEADROOM_BEGIN + "\n"
            "| Imagem | `.bin` assinado | Folga |\n|---|---:|---:|\n"
            + "".join(r + "\n" for r in rows)
            + cfb.HEADROOM_END + "\n\ntext after\n")


GOOD = doc("| `pico_w_release` | 1.028.493 | 11.891 |",
           "| `pico_w_alpha` | 986.525 | 53.859 |",
           "| `pico_w_test_https` | isenta | — |")

check("a table that matches the record passes",
      cfb.headroom_table_errors(GOOD, CFG, SAFE, TRAILER) == [],
      str(cfb.headroom_table_errors(GOOD, CFG, SAFE, TRAILER)))

stale = doc("| `pico_w_release` | 1.028.493 | 13.196 |",
            "| `pico_w_alpha` | 986.525 | 53.859 |",
            "| `pico_w_test_https` | isenta | — |")
errs = cfb.headroom_table_errors(stale, CFG, SAFE, TRAILER)
check("a stale margin fails, naming the image",
      len(errs) == 1 and "pico_w_release" in errs[0], str(errs))

missing = doc("| `pico_w_release` | 1.028.493 | 11.891 |",
              "| `pico_w_test_https` | isenta | — |")
errs = cfb.headroom_table_errors(missing, CFG, SAFE, TRAILER)
check("an image with no row fails", len(errs) == 1 and "pico_w_alpha" in errs[0], str(errs))

extra = doc("| `pico_w_release` | 1.028.493 | 11.891 |",
            "| `pico_w_alpha` | 986.525 | 53.859 |",
            "| `pico_w_test_https` | isenta | — |",
            "| `pico_w_debug` | 1.000.000 | 40.384 |")
errs = cfb.headroom_table_errors(extra, CFG, SAFE, TRAILER)
check("a row for an image with no budget fails", len(errs) == 1 and "pico_w_debug" in errs[0], str(errs))

numbered = doc("| `pico_w_release` | 1.028.493 | 11.891 |",
               "| `pico_w_alpha` | 986.525 | 53.859 |",
               "| `pico_w_test_https` | 1.040.237 | 147 |")
errs = cfb.headroom_table_errors(numbered, CFG, SAFE, TRAILER)
check("an exempt image shown with a margin fails",
      len(errs) == 1 and "pico_w_test_https" in errs[0], str(errs))

nobin = {"envs": dict(CFG["envs"], pico_w_air={"budget": 1, "measured": 1}),
         "ota_exempt": CFG["ota_exempt"]}
errs = cfb.headroom_table_errors(GOOD, nobin, SAFE, TRAILER)
check("an image without a recorded .bin fails", any("pico_w_air" in e for e in errs), str(errs))

errs = cfb.headroom_table_errors("no table here\n", CFG, SAFE, TRAILER)
check("a document without the table fails", len(errs) == 1, str(errs))

fail, _ = cfb.bin_vs_record(1028253, 1028252)
check("a .bin that grew past its record fails", fail)
fail, msg = cfb.bin_vs_record(1028252, 1028252)
check("a .bin equal to its record passes, silently", not fail and msg is None, str(msg))
fail, msg = cfb.bin_vs_record(1028000, 1028252)
check("a .bin that shrank passes, with a note", not fail and msg is not None, str(msg))
fail, _ = cfb.bin_vs_record(1028252, None)
check("a .bin with no record fails", fail)

check("thousands in pt-BR, en and bare",
      cfb.table_int("1.028.493") == 1028493 and cfb.table_int("1,028,493") == 1028493
      and cfb.table_int("1028493") == 1028493 and cfb.table_int("isenta") is None)

# The gate prints the row it wants; pasting those rows has to give a table it accepts —
# for every image, the exempt one included, and for an image over the ceiling too.
rows = cfb.headroom_rows(CFG, SAFE, TRAILER)
check("the rows the gate suggests make a table it accepts",
      sorted(rows) == sorted(CFG["envs"])
      and cfb.headroom_table_errors(doc(*rows.values()), CFG, SAFE, TRAILER) == [],
      str(rows))
over = {"envs": {"pico_w_release": {"budget": 1, "measured": 1, "bin": SAFE - TRAILER + 5}}}
row = cfb.headroom_rows(over, SAFE, TRAILER)["pico_w_release"]
check("an image over the ceiling gets a row with a negative margin, and it reads back",
      row.endswith("| -5 |") and cfb.headroom_table_errors(doc(row), over, SAFE, TRAILER) == [],
      row)

# An image that updates into the other slot of a partition table (the RP2350
# since step 4 of docs/analysis/OTA_AB_RP2350.md) is held to that slot over the
# air, not to the Pico W's staging ceiling: its row measures the slot.
SLOT = 1568768
slotted = {"envs": {"pico2_w_release": {"budget": 1, "measured": 1, "bin": 993392, "slot": SLOT}}}
row = cfb.headroom_rows(slotted, SAFE, TRAILER)["pico2_w_release"]
check("an image with a slot gets its margin against the slot",
      row == f"| `pico2_w_release` | {cfb.pt_int(993392 + TRAILER)} | {cfb.pt_int(SLOT - 993392 - TRAILER)} |"
      and cfb.headroom_table_errors(doc(row), slotted, SAFE, TRAILER) == [], row)
errs = cfb.headroom_table_errors(
    doc(f"| `pico2_w_release` | {cfb.pt_int(993392 + TRAILER)} | {cfb.pt_int(SAFE - 993392 - TRAILER)} |"),
    slotted, SAFE, TRAILER)
check("a slotted image's row measured against the Pico W's ceiling fails",
      len(errs) == 1 and "pico2_w_release" in errs[0], str(errs))
check("the OTA ceiling is the slot when there is one, the Pico W's otherwise",
      cfb.ota_ceiling({"slot": SLOT}, SAFE) == SLOT and cfb.ota_ceiling({}, SAFE) == SAFE)

# The gate as CI runs it (the env and its build log), on a copy of it in a
# temporary tree. Until 2026-10-24 an image whose firmware.bin was not there
# got a NOTE and exit 0, so a run after .pio/build was cleaned passed without
# reading any .bin (found in S10c D5's review, 2026-10-23); an ota_layout.h it
# could not read did the same. Not looking is a failure, not a pass.
GATE = os.path.abspath(cfb.__file__)
BIN = 1028252
LOG = "Flash: [==========]  95.7% (used 1000000 bytes from 1044480 bytes)\n"


def run_gate(env, bin_size=None, layout_edit=None):
    """(exit, output) of the copied gate for @p env, with a firmware.bin of
    @p bin_size bytes under .pio/build/<env>/ unless it is None."""
    with tempfile.TemporaryDirectory() as tmp:
        os.makedirs(os.path.join(tmp, "tools"))
        os.makedirs(os.path.join(tmp, "src", "ota"))
        shutil.copy(GATE, os.path.join(tmp, "tools"))
        for h in ("ota_layout.h", "signature.h"):
            shutil.copy(os.path.join(cfb.ROOT, "src", "ota", h), os.path.join(tmp, "src", "ota"))
        if layout_edit:
            path = os.path.join(tmp, "src", "ota", "ota_layout.h")
            with open(path, encoding="utf-8") as fh:
                text = fh.read()
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(layout_edit(text))
        with open(os.path.join(tmp, "tools", "flash_budget.json"), "w", encoding="utf-8") as fh:
            json.dump({"ceiling": 1044480,
                       "envs": {"pico_w_release": {"budget": 1000000, "measured": 1000000, "bin": BIN},
                                "pico_w_test_https": {"budget": 1000000, "measured": 1000000}},
                       "ota_exempt": {"pico_w_test_https": "bench image, flashed over USB"}}, fh)
        log = os.path.join(tmp, "build.log")
        with open(log, "w", encoding="utf-8") as fh:
            fh.write(LOG)
        if bin_size is not None:
            os.makedirs(os.path.join(tmp, ".pio", "build", env))
            with open(os.path.join(tmp, ".pio", "build", env, "firmware.bin"), "wb") as fh:
                fh.write(b"\xff" * bin_size)
        r = subprocess.run([sys.executable, os.path.join(tmp, "tools", "check_flash_budget.py"), env, log],
                           capture_output=True, text=True)
        return r.returncode, (r.stdout + r.stderr).strip()


code, out = run_gate("pico_w_release")
check("an image whose firmware.bin is not there fails, naming where it looked",
      code == 1 and "FAIL" in out and ".pio/build/pico_w_release/firmware.bin" in out, f"exit {code}: {out}")
code, out = run_gate("pico_w_release", BIN)
check("a firmware.bin of its recorded size passes",
      code == 0 and "OK pico_w_release: firmware.bin signed" in out, f"exit {code}: {out}")
code, out = run_gate("pico_w_release", BIN + 1)
check("a firmware.bin 1 B over its record fails", code == 1, f"exit {code}: {out}")
code, out = run_gate("pico_w_test_https")
check("an image exempt from the OTA ceiling needs no firmware.bin",
      code == 0 and "SKIP pico_w_test_https" in out, f"exit {code}: {out}")
code, out = run_gate("pico_w_release", BIN,
                     lambda t: re.sub(r"#define\s+OTA_FLASH_TOTAL\b", "#define OTA_FLASH_TOTAL_GONE", t))
check("an ota_layout.h the gate cannot read fails, naming OTA_APP_SAFE_MAX_SIZE",
      code == 1 and "FAIL" in out and "OTA_APP_SAFE_MAX_SIZE" in out, f"exit {code}: {out}")

print(f"{len(RAN) - len(FAILS)} ok, {len(FAILS)} falha(s)")
sys.exit(1 if FAILS else 0)
