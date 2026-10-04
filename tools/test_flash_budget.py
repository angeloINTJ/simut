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
"""
import os
import sys

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

print(f"{len(RAN) - len(FAILS)} ok, {len(FAILS)} falha(s)")
sys.exit(1 if FAILS else 0)
